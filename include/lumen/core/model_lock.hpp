// Lumen — model lock.
//
// One recursive mutex guards the lighting model (channels, rooms, groups,
// scenes), the hardware driver set and config documents. Holders: the
// lighting tick, input polling, automation evaluation and every API
// request. Critical sections are short (no network I/O or sleeps while
// holding it), so a single lock keeps the concurrency model auditable
// without risking lock-order inversions between subsystems.
#pragma once

#include <mutex>

namespace lumen
{

std::recursive_mutex &model_mutex();

using ModelLock = std::lock_guard<std::recursive_mutex>;

} // namespace lumen
