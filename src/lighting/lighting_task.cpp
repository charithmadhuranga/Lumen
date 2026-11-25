// Lumen — lighting engine implementation (src/lighting/lighting_task.cpp)
//
// Pure C++ core: bounded command queue, transition/effect processing and
// hardware writes via driver adapters. No FreeRTOS dependency.
#include "lumen/lighting/lighting_task.h"

#include <cstring>
#include <utility>

namespace lumen
{

namespace
{

struct EffectName
{
    EffectType type;
    const char *name;
};

constexpr EffectName kEffects[] = {
    {EffectType::kBreathing, "breathing"},   {EffectType::kPulse, "pulse"},
    {EffectType::kRainbow, "rainbow"},       {EffectType::kColorCycle, "color_cycle"},
    {EffectType::kCandlelight, "candlelight"}, {EffectType::kSunrise, "sunrise"},
    {EffectType::kSunset, "sunset"},         {EffectType::kStrobe, "strobe"},
    {EffectType::kCustomSequence, "custom_sequence"},
};

bool same_state(const LightState &a, const LightState &b)
{
    return a.power == b.power && a.brightness == b.brightness && a.red == b.red &&
           a.green == b.green && a.blue == b.blue && a.white == b.white &&
           a.color_temperature == b.color_temperature;
}

// Interpolation endpoints: an "off" endpoint becomes the other endpoint's
// colour at brightness 0, so fades in/out never pass through black/odd
// colours and the brightness ramps smoothly.
void fade_endpoints(LightState &from, LightState &to)
{
    if (!from.power) {
        const LightState keep = to;
        from = keep;
        from.brightness = 0;
        from.power = true;
    }
    if (!to.power) {
        const LightState keep = from;
        to = keep;
        to.brightness = 0;
        to.power = true;
    }
}

} // namespace

bool effect_type_from_string(const std::string &name, EffectType &out)
{
    for (const auto &e : kEffects) {
        if (name == e.name) {
            out = e.type;
            return true;
        }
    }
    return false;
}

const char *effect_type_name(EffectType type)
{
    for (const auto &e : kEffects) {
        if (e.type == type) {
            return e.name;
        }
    }
    return "none";
}

// ---------------------------------------------------------------------------
// Hardware binding
// ---------------------------------------------------------------------------

void LightingTask::bind_slot(std::size_t index)
{
    Slot &slot = slots_[index];
    const Channel &ch = channel_manager_.at(index);
    slot.adapter.reset();
    slot.adapter_ok = false;
    slot.written_valid = false; // force a full rewrite on the new binding
    if (hw_ == nullptr) {
        channel_manager_.set_available(ch.id, false);
        return;
    }
    slot.adapter = create_driver_adapter(ch.id, ch.type, ch.driver, *hw_);
    if (slot.adapter && slot.adapter->initialize() == ESP_OK) {
        slot.adapter_ok = true;
    }
    channel_manager_.set_available(ch.id, slot.adapter_ok);
}

void LightingTask::attach_hardware(IHardwareManager *hw)
{
    hw_ = hw;
    for (std::size_t i = 0; i < channel_manager_.channel_count(); ++i) {
        bind_slot(i);
    }
}

void LightingTask::detach_hardware()
{
    for (std::size_t i = 0; i < channel_manager_.channel_count(); ++i) {
        Slot &slot = slots_[i];
        if (slot.adapter_ok && slot.adapter) {
            slot.adapter->safe_state();
        }
        slot.adapter.reset();
        slot.adapter_ok = false;
        slot.written_valid = false;
        channel_manager_.set_available(channel_manager_.at(i).id, false);
    }
    if (hw_ != nullptr) {
        hw_->flush_outputs();
    }
    hw_ = nullptr;
}

// ---------------------------------------------------------------------------
// Queue
// ---------------------------------------------------------------------------

EnqueueResult LightingTask::enqueue(const LightCommandEnvelope &cmd)
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    if (queue_count_ >= queue_.size()) {
        stats_.queue_dropped++;
        return EnqueueResult::kQueueFull;
    }
    queue_[queue_tail_] = cmd;
    queue_tail_ = (queue_tail_ + 1) % queue_.size();
    queue_count_++;
    return EnqueueResult::kOk;
}

std::size_t LightingTask::queue_depth() const
{
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_count_;
}

std::size_t LightingTask::process(uint32_t now_ms, std::size_t max_commands)
{
    std::size_t processed = 0;
    while (processed < max_commands) {
        LightCommandEnvelope env;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_count_ == 0) {
                break;
            }
            env = std::move(queue_[queue_head_]);
            queue_head_ = (queue_head_ + 1) % queue_.size();
            queue_count_--;
        }
        apply_command(env.command, now_ms);
        processed++;
    }
    return processed;
}

void LightingTask::apply_command(const LightCommand &cmd, uint32_t now_ms)
{
    const std::size_t index = channel_manager_.index_of(cmd.channel_id);
    if (index == ChannelManager::kNotFound) {
        stats_.commands_rejected++;
        return;
    }
    Channel &ch = channel_manager_.at(index);
    Slot &slot = slots_[index];

    // Merge the fields this command sets into the current target.
    LightState target = ch.requested;
    const LightState &s = cmd.state;
    if (cmd.fields & kFieldPower) target.power = s.power;
    if (cmd.fields & kFieldBrightness) {
        target.brightness = s.brightness;
        if (!(cmd.fields & kFieldPower)) {
            target.power = s.brightness > 0; // dimming to 0 turns off, >0 turns on
        }
    }
    if (cmd.fields & kFieldColor) {
        target.red = s.red;
        target.green = s.green;
        target.blue = s.blue;
        if (!(cmd.fields & kFieldPower)) target.power = true;
    }
    if (cmd.fields & kFieldWhite) target.white = s.white;
    if (cmd.fields & kFieldCct) {
        target.color_temperature = s.color_temperature;
        if (!(cmd.fields & kFieldPower)) target.power = true;
    }
    // Turning on a light whose remembered brightness is 0 -> full.
    if (target.power && target.brightness == 0 && ch.type != LightType::kSwitch) {
        target.brightness = 100;
    }
    // Colour lights with no colour yet default to white.
    if (target.power && (ch.type == LightType::kRgb || ch.type == LightType::kRgbw ||
                         ch.type == LightType::kAddressableRgb ||
                         ch.type == LightType::kAddressableRgbw) &&
        target.red == 0 && target.green == 0 && target.blue == 0 && target.white == 0) {
        target.red = target.green = target.blue = 255;
    }
    target = normalize_for_channel(ch, target);
    ch.requested = target;
    state_dirty_ = true;
    stats_.commands_processed++;

    // Effects: a plain command cancels any running effect (SPEC §8.6).
    EffectType effect_type{};
    if (!cmd.effect.empty() && cmd.effect != "stop" && cmd.effect != "none" &&
        effect_type_from_string(cmd.effect, effect_type)) {
        EffectParams params;
        params.type = effect_type;
        params.speed_ms = cmd.effect_speed_ms != 0 ? cmd.effect_speed_ms : 2000;
        params.duration_ms = cmd.effect_duration_ms;
        if (params.duration_ms == 0 &&
            (effect_type == EffectType::kSunrise || effect_type == EffectType::kSunset ||
             effect_type == EffectType::kColorCycle)) {
            params.duration_ms = 600000; // one-shot effects: 10 min default
        }
        if (params.duration_ms == 0) {
            params.duration_ms = UINT32_MAX; // run until replaced
        }
        params.max_brightness = target.brightness > 0 ? target.brightness : 100;
        params.min_brightness = params.max_brightness / 10;
        if (target.red != 0 || target.green != 0 || target.blue != 0) {
            if (effect_type != EffectType::kCandlelight) {
                params.max_color_r = target.red;
                params.max_color_g = target.green;
                params.max_color_b = target.blue;
            }
        }
        effects_.start(ch.id, params, now_ms);
        slot.effect = cmd.effect;
        slot.transition.active = false;
        notify(index, LightChange::kRequested);
        return;
    }
    effects_.cancel(ch.id);
    slot.effect.clear();

    const uint32_t duration = cmd.use_default_transition ? ch.transition_ms : cmd.transition_ms;
    LightState from = slot.written_valid ? slot.written : ch.applied;
    if (duration > 1 && !same_state(from, target)) {
        LightState to = target;
        fade_endpoints(from, to);
        Transition &tr = slot.transition;
        tr.active = true;
        tr.start_ms = now_ms;
        tr.duration_ms = duration;
        tr.start_state = from;
        tr.end_state = to;
        tr.ease = ease_smooth;
        slot.target = target;
    } else {
        slot.transition.active = false;
        slot.target = target;
    }
    notify(index, LightChange::kRequested);
}

// ---------------------------------------------------------------------------
// Tick
// ---------------------------------------------------------------------------

bool LightingTask::write_slot(std::size_t index, const LightState &out)
{
    Slot &slot = slots_[index];
    Channel &ch = channel_manager_.at(index);
    if (slot.written_valid && same_state(slot.written, out)) {
        return true; // redundant write skipped
    }
    ch.applied = out;
    if (!slot.adapter_ok || !slot.adapter) {
        return false;
    }
    const DriverDuty duty = state_to_duties(ch, out, ch.calibration);
    stats_.hardware_writes++;
    if (slot.adapter->apply(duty) != AdapterResult::kOk) {
        stats_.hardware_errors++;
        slot.written_valid = false; // retry next tick
        return false;
    }
    slot.written = out;
    slot.written_valid = true;
    return true;
}

void LightingTask::tick(uint32_t now_ms)
{
    stats_.ticks++;
    for (std::size_t i = 0; i < channel_manager_.channel_count(); ++i) {
        Slot &slot = slots_[i];
        Channel &ch = channel_manager_.at(i);
        LightState out = ch.requested;
        bool settled = false;

        if (!slot.effect.empty()) {
            LightState fx{};
            if (effects_.compute(ch.id, now_ms, fx)) {
                out = normalize_for_channel(ch, fx);
                if (!fx.power) {
                    out.power = false;
                }
            } else {
                // One-shot effect finished: its last frame becomes the state.
                effects_.cancel(ch.id);
                slot.effect.clear();
                out = slot.written_valid ? slot.written : ch.requested;
                ch.requested = out;
                state_dirty_ = true;
                settled = true;
            }
        } else if (slot.transition.active) {
            LightState frame{};
            const bool still_active = transition_tick(slot.transition, now_ms, frame);
            if (still_active) {
                frame.power = frame.brightness > 0;
                out = frame;
            } else {
                out = slot.target; // exact snap to the requested target
                settled = true;
            }
        }

        write_slot(i, out);
        if (settled) {
            notify(i, LightChange::kSettled);
        }
    }
    if (hw_ != nullptr) {
        if (hw_->flush_outputs() != ESP_OK) {
            stats_.hardware_errors++;
            // Force rewrite of PCA-backed channels next tick.
            for (std::size_t i = 0; i < channel_manager_.channel_count(); ++i) {
                if (channel_manager_.at(i).driver.type == "pca9685") {
                    slots_[i].written_valid = false;
                }
            }
        }
    }
}

void LightingTask::run_once(uint32_t now_ms)
{
    process(now_ms, kLightingCommandQueueSize);
    tick(now_ms);
}

// ---------------------------------------------------------------------------
// Registry
// ---------------------------------------------------------------------------

bool LightingTask::register_channel(const Channel &channel)
{
    if (!channel_manager_.register_channel(channel)) {
        return false;
    }
    const std::size_t index = channel_manager_.channel_count() - 1;
    Channel &ch = channel_manager_.at(index);
    ch.requested = normalize_for_channel(ch, ch.requested);
    slots_[index] = Slot{};
    slots_[index].target = ch.requested;
    bind_slot(index);
    notify(index, LightChange::kConfig);
    return true;
}

bool LightingTask::update_channel(const Channel &channel)
{
    const std::size_t index = channel_manager_.index_of(channel.id);
    if (index == ChannelManager::kNotFound) {
        return false;
    }
    Channel &ch = channel_manager_.at(index);
    const bool rebind = ch.driver.type != channel.driver.type ||
                        ch.driver.device_id != channel.driver.device_id ||
                        ch.driver.channel != channel.driver.channel || ch.type != channel.type;
    if (rebind && slots_[index].adapter_ok && slots_[index].adapter) {
        slots_[index].adapter->safe_state(); // release the old outputs
    }
    const LightState requested = ch.requested;
    const LightState applied = ch.applied;
    ch = channel;
    ch.requested = normalize_for_channel(ch, requested);
    ch.applied = applied;
    slots_[index].target = ch.requested;
    slots_[index].transition.active = false;
    if (rebind) {
        bind_slot(index);
    } else {
        slots_[index].written_valid = false; // limits/calibration may differ
    }
    notify(index, LightChange::kConfig);
    return true;
}

bool LightingTask::remove_channel(const std::string &channel_id)
{
    const std::size_t index = channel_manager_.index_of(channel_id);
    if (index == ChannelManager::kNotFound) {
        return false;
    }
    if (slots_[index].adapter_ok && slots_[index].adapter) {
        slots_[index].adapter->safe_state();
        if (hw_ != nullptr) {
            hw_->flush_outputs();
        }
    }
    effects_.cancel(channel_id);
    channel_manager_.remove_channel(channel_id);
    // Keep slots aligned with the shifted channel table.
    for (std::size_t i = index; i + 1 < slots_.size(); ++i) {
        slots_[i] = std::move(slots_[i + 1]);
    }
    slots_[slots_.size() - 1] = Slot{};
    return true;
}

bool LightingTask::get_requested(const std::string &channel_id, LightState &out) const
{
    return channel_manager_.get_requested(channel_id, out);
}

bool LightingTask::get_applied(const std::string &channel_id, LightState &out) const
{
    const Channel *ch = channel_manager_.find(channel_id);
    if (ch == nullptr) return false;
    out = ch->applied;
    return true;
}

bool LightingTask::hardware_ok(const std::string &channel_id) const
{
    const std::size_t index = channel_manager_.index_of(channel_id);
    return index != ChannelManager::kNotFound && slots_[index].adapter_ok;
}

bool LightingTask::animating(const std::string &channel_id) const
{
    const std::size_t index = channel_manager_.index_of(channel_id);
    return index != ChannelManager::kNotFound &&
           (slots_[index].transition.active || !slots_[index].effect.empty());
}

std::string LightingTask::active_effect(const std::string &channel_id) const
{
    const std::size_t index = channel_manager_.index_of(channel_id);
    return index == ChannelManager::kNotFound ? std::string() : slots_[index].effect;
}

bool LightingTask::restore_state(const std::string &channel_id, const LightState &state)
{
    const std::size_t index = channel_manager_.index_of(channel_id);
    if (index == ChannelManager::kNotFound) {
        return false;
    }
    Channel &ch = channel_manager_.at(index);
    ch.requested = normalize_for_channel(ch, state);
    slots_[index].target = ch.requested;
    slots_[index].transition.active = false;
    return true;
}

bool LightingTask::take_state_dirty()
{
    const bool d = state_dirty_;
    state_dirty_ = false;
    return d;
}

void LightingTask::notify(std::size_t index, LightChange change)
{
    if (listener_ != nullptr) {
        listener_(channel_manager_.at(index), change, listener_user_);
    }
}

} // namespace lumen
