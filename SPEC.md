# Lumen — ESP32 Smart Lighting Controller
## Complete Product Requirements, Firmware Architecture & Coding-Agent Implementation Specification

**Project:** Lumen  
**Firmware:** ESP-IDF + PlatformIO + C++17  
**Frontend:** React + TypeScript + Vite  
**Reference Hardware:** KinCony KC868-AP  
**Target Hardware:** ESP32, ESP32-S3 and supported ESP32 variants  
**Architecture:** Hardware Abstraction Layer + Event-Driven Lighting Engine  
**Version:** 1.0  
**Status:** Implementation Specification

---

# 1. Product Overview

## 1.1 Vision

Build a complete, universal smart lighting controller firmware platform for ESP32 devices.

The platform must support different ESP32 boards through configurable hardware profiles, providing a unified lighting control system independent of the underlying hardware.

The firmware must combine:

- Professional smart lighting control.
- PWM dimming.
- RGB, RGBW and CCT lighting.
- Addressable LED control.
- Relay-based lighting.
- Room and group management.
- Scene management.
- Interactive SVG floor-plan lighting visualization.
- Local automation engine.
- Visual automation editor.
- ESP-NOW multi-controller synchronization.
- MQTT communication.
- Home Assistant MQTT Discovery.
- TCP and UDP protocols.
- Wi-Fi station and access point modes.
- Ethernet support.
- Responsive WebUI.
- Authentication and configuration management.
- OTA firmware updates.

The firmware must work independently without requiring Home Assistant, cloud services or an external automation server.

## 1.2 Core design principles

1. Hardware independence: the lighting application must never directly depend on physical GPIO numbers.
2. Offline-first: lighting and automation must work without internet connectivity.
3. Configuration-driven: hardware profiles, lighting channels and automation rules must be configurable.
4. Modular: hardware drivers, lighting, networking and application services must be independently testable.
5. Extensible: new ESP32 boards and peripheral drivers must be addable without rewriting the application.
6. Reliable: invalid configuration, network failure and peripheral failure must not crash the lighting engine.
7. User-friendly: ordinary lighting operations must be simple and intuitive.
8. Professional: the UI must resemble a commercial lighting management application, not an engineering diagnostic tool.

## 1.3 Product terminology

| Term | Definition |
|---|---|
| Controller | A physical ESP32 device running the firmware |
| Board Profile | Hardware-specific pin and peripheral configuration |
| Output | Physical hardware output |
| Lighting Channel | Logical controllable light |
| Room | Logical location containing lights |
| Group | Collection of lighting channels |
| Scene | Saved collection of lighting states |
| Automation | Trigger-condition-action workflow |
| Floor Plan | SVG representation of a building or room layout |
| Controller Network | Collection of synchronized controllers |
| Device Registry | Registry of hardware and logical devices |

---

# 2. Functional Requirements

## 2.1 Lighting capabilities

The firmware must support:

| Capability | Required |
|---|---|
| Single-channel PWM | Yes |
| Relay ON/OFF | Yes |
| RGB | Yes |
| RGBW | Yes |
| CCT / Tunable white | Yes |
| Addressable LEDs | Yes |
| Brightness control | Yes |
| Color control | Yes |
| Smooth transitions | Yes |
| Lighting effects | Yes |
| Scenes | Yes |
| Groups | Yes |
| Scheduling | Yes |
| Physical switch inputs | Yes |
| Automation | Yes |
| Multi-controller synchronization | Yes |

## 2.2 Communication protocols

| Protocol | Required functionality |
|---|---|
| Wi-Fi | Station and AP |
| Ethernet | Supported PHY profiles |
| MQTT | Commands, states and events |
| MQTT Discovery | Home Assistant |
| ESP-NOW | Controller synchronization |
| UDP | Discovery, events and control |
| TCP | Client/server communication |
| RS485 | UART and Modbus RTU transport |
| HTTP | REST API |
| WebSocket | Real-time UI updates |

## 2.3 Authentication

Default initial credentials:

- Username: `admin`
- Password: `admin`

The first authenticated setup must require a password change.

The platform is initially single-user. Multiple user roles are outside the initial release scope.

---

# 3. Technology Stack

## 3.1 Firmware

| Component | Technology |
|---|---|
| MCU framework | ESP-IDF |
| Build system | PlatformIO |
| Language | C++17 |
| RTOS | FreeRTOS |
| Hardware drivers | ESP-IDF |
| Configuration | JSON |
| Runtime persistence | NVS |
| Filesystem | LittleFS |
| Networking | ESP-IDF / lwIP |
| MQTT | ESP-MQTT |
| HTTP server | ESP-IDF HTTP Server |
| WebSocket | ESP-IDF HTTP Server |
| OTA | ESP-IDF OTA |
| PWM | LEDC / PCA9685 |
| Addressable LEDs | RMT |
| Wireless synchronization | ESP-NOW |

Use ESP-IDF APIs as the primary hardware and networking interfaces. Do not introduce Arduino framework dependencies.

Pin the ESP-IDF version and PlatformIO platform/package versions in the project configuration. Do not automatically upgrade dependencies during implementation.

## 3.2 Frontend

| Concern | Technology |
|---|---|
| Framework | React + TypeScript + Vite |
| UI components | shadcn/ui (Radix primitives, source-copied into the repo) |
| CSS | Tailwind CSS |
| Animation | Motion for React (`motion/react`) |
| Advanced sequences | GSAP (optional, lazy-loaded) |
| Floor-plan editor | React Konva |
| Live floor-plan rendering | Inline sanitized SVG + Motion |
| Automation editor | React Flow (`@xyflow/react`) |
| Icons | Lucide React |
| State management | Zustand |
| API state | TanStack Query |
| Forms | React Hook Form |
| Validation | Zod |
| Charts | Recharts |
| Notifications | Sonner |
| Routing | React Router |

Rules:

- Keep the frontend modular and lightweight. The bundle is served from ESP32 flash.
- Do not put hardware-specific logic in the frontend.
- Zustand holds client and live state (WebSocket-pushed light state, UI mode, editor drafts). TanStack Query holds server state (REST resources). Do not duplicate the same data in both.
- Zod schemas are the single source of truth for API payload types; derive TypeScript types with `z.infer`.
- Heavy libraries (React Konva, React Flow, Recharts, GSAP) must be route-level lazy-loaded.
- GSAP is optional. Nothing required for core operation may depend on it.
- Pin exact versions in `package-lock.json`. Do not auto-upgrade during implementation.
- The complete UI design specification is in `DESIGN.md`.

## 3.3 Build output

The React application must be compiled into static assets and embedded in a LittleFS filesystem image.

The firmware serves:

- `index.html`
- JavaScript bundles
- CSS
- Icons
- Fonts
- Static application assets

Use compressed assets (pre-gzipped at build time, served with `Content-Encoding: gzip`) and immutable caching for versioned bundles.

Target a total gzipped size budget as defined in `DESIGN.md` section 14. A build that exceeds the budget must fail CI.

---

# 4. System Architecture

## 4.1 High-level architecture

```text
                    USER
                     |
                     v
             REACT WEB APPLICATION
                     |
              HTTP REST / WS
                     |
                     v
              WEB API SERVER
                     |
                     v
               APPLICATION CORE
                     |
          +----------+----------+
          |          |          |
          v          v          v
      LIGHTING   AUTOMATION   DEVICE
       ENGINE      ENGINE    REGISTRY
          |          |          |
          +----------+----------+
                     |
                  EVENT BUS
                     |
          +----------+-----------+
          |          |           |
          v          v           v
       HARDWARE   NETWORKING   STORAGE
       MANAGER     MANAGER     MANAGER
          |          |
          |      +---+----+------+
          |      |        |      |
          v      v        v      v
        GPIO    MQTT    ESP-NOW TCP/UDP
        PWM      HA
        RELAY
        RMT
        I2C
        RS485
```

## 4.2 Architectural boundaries

### Hardware layer

Responsible for:

- GPIO.
- I2C.
- UART.
- PWM.
- Relay outputs.
- LED drivers.
- Input expansion.
- Ethernet PHY.
- Addressable LED peripherals.

### Application layer

Responsible for:

- Lighting channels.
- Rooms.
- Groups.
- Scenes.
- Automations.
- Scheduling.
- Controller synchronization.

### Infrastructure layer

Responsible for:

- Networking.
- Persistence.
- OTA.
- Authentication.
- Diagnostics.
- Web server.

### Presentation layer

Responsible for:

- Dashboard.
- Lighting controls.
- Floor-plan visualization.
- Configuration.
- Automation editors.
- Monitoring.

No application module may include board-specific GPIO definitions.

---

# 5. Hardware Abstraction Layer

## 5.1 HAL requirements

Implement a unified hardware abstraction layer.

Required interfaces:

```cpp
class IOutputDriver {
public:
    virtual esp_err_t initialize() = 0;
    virtual esp_err_t set_value(uint32_t value) = 0;
    virtual esp_err_t get_value(uint32_t& value) = 0;
    virtual esp_err_t shutdown() = 0;
    virtual ~IOutputDriver() = default;
};

class IDigitalInputDriver {
public:
    virtual esp_err_t initialize() = 0;
    virtual esp_err_t read(bool& state) = 0;
    virtual ~IDigitalInputDriver() = default;
};

class IPwmDriver {
public:
    virtual esp_err_t initialize() = 0;
    virtual esp_err_t set_duty(uint16_t duty) = 0;
    virtual esp_err_t set_frequency(uint32_t frequency) = 0;
    virtual ~IPwmDriver() = default;
};
```

Additional interfaces:

- `II2CBus`
- `IUartDriver`
- `IEthernetDriver`
- `IAddressableLedDriver`
- `IRelayDriver`
- `IPeripheralDriver`

All hardware interfaces must report initialization and runtime errors.

## 5.2 Hardware capability registry

Every board must declare its available capabilities.

Example:

```json
{
  "capabilities": {
    "native_pwm": true,
    "i2c": true,
    "uart": true,
    "ethernet": true,
    "esp_now": true,
    "rmt": true,
    "relay": true,
    "digital_input": true,
    "external_pwm": true,
    "addressable_led": true
  }
}
```

Capabilities describe what the MCU and board can actually provide.

Do not assume that every capability is physically connected just because the MCU supports it.

## 5.3 Pin validation

Implement a dedicated `PinValidator`.

Validation must include:

- GPIO existence.
- Input/output restrictions.
- Input-only GPIOs.
- Boot-strapping pins.
- Flash/PSRAM reserved pins.
- Peripheral conflicts.
- Duplicate GPIO assignments.
- UART conflicts.
- I2C conflicts.
- Ethernet conflicts.
- Native PWM channel limitations.
- RMT resource limitations.
- Board-specific reserved pins.

Pin capabilities must be MCU-specific.

A board profile must explicitly identify shared resources.

Do not silently assign a pin to two active peripherals.

## 5.4 Runtime reconfiguration

Changing a board profile must use a transactional workflow:

1. Validate candidate profile.
2. Save temporary candidate.
3. Stop affected hardware services.
4. Place outputs into configured safe states.
5. Initialize new hardware.
6. Run peripheral diagnostics.
7. Commit configuration on success.
8. Restore the previous profile on failure.

A failed profile change must not erase the last working configuration.

---

# 6. Board Profile System

## 6.1 Profile types

Two categories:

### Built-in profiles

Bundled with firmware:

- KinCony KC868-AP.
- Generic ESP32 DevKit.
- Generic ESP32-S3.
- Generic ESP32 + PCA9685.
- Generic ESP32 + PCF8574.
- Generic ESP32 PWM controller.

### Custom profiles

Created using the WebUI.

Users can:

- Create profiles.
- Duplicate profiles.
- Edit pin mappings.
- Add peripheral devices.
- Assign hardware outputs.
- Validate configurations.
- Import JSON.
- Export JSON.
- Restore factory profiles.

Built-in profiles must be protected from accidental modification. Users may duplicate them to create custom variants.

## 6.2 Board profile JSON schema

File: `schemas/board-profile.schema.json`

```json
{
  "$schema": "https://json-schema.org/draft/2020-12/schema",
  "title": "Board Profile",
  "type": "object",
  "required": [
    "schema_version",
    "id",
    "name",
    "mcu",
    "resources",
    "peripherals"
  ],
  "properties": {
    "schema_version": {
      "type": "integer",
      "minimum": 1
    },
    "id": {
      "type": "string",
      "pattern": "^[a-z0-9_-]+$"
    },
    "name": {
      "type": "string",
      "minLength": 1,
      "maxLength": 80
    },
    "manufacturer": {
      "type": "string"
    },
    "mcu": {
      "type": "string",
      "enum": ["esp32", "esp32s3", "esp32c3", "esp32c6"]
    },
    "description": {
      "type": "string"
    },
    "resources": {
      "type": "array",
      "items": {
        "$ref": "#/$defs/resource"
      }
    },
    "peripherals": {
      "type": "array",
      "items": {
        "$ref": "#/$defs/peripheral"
      }
    },
    "capabilities": {
      "type": "object"
    }
  },
  "$defs": {
    "resource": {
      "type": "object",
      "required": ["id", "type"],
      "properties": {
        "id": {"type": "string"},
        "type": {
          "type": "string",
          "enum": [
            "gpio",
            "pwm",
            "i2c",
            "uart",
            "rmt",
            "ethernet"
          ]
        },
        "gpio": {
          "type": "integer",
          "minimum": 0,
          "maximum": 48
        },
        "direction": {
          "enum": ["input", "output", "bidirectional"]
        },
        "inverted": {
          "type": "boolean"
        },
        "pull": {
          "enum": ["none", "up", "down"]
        },
        "safe_state": {
          "type": ["boolean", "integer", "null"]
        }
      }
    },
    "peripheral": {
      "type": "object",
      "required": ["id", "type"],
      "properties": {
        "id": {"type": "string"},
        "type": {
          "enum": [
            "pca9685",
            "pcf8574",
            "relay",
            "ethernet",
            "rs485",
            "addressable_led"
          ]
        },
        "bus": {"type": "string"},
        "address": {
          "type": "integer",
          "minimum": 0,
          "maximum": 127
        },
        "channels": {
          "type": "integer",
          "minimum": 1,
          "maximum": 64
        },
        "frequency": {
          "type": "integer",
          "minimum": 1
        },
        "settings": {
          "type": "object"
        }
      },
      "additionalProperties": true
    }
  }
}
```

The schema is a structural validation layer, not a substitute for MCU-specific semantic validation.

Production validation must additionally verify that every referenced resource exists and that peripheral combinations are physically valid.

## 6.3 KC868-AP reference profile

File: `boards/kincony_kc868_ap.json`

```json
{
  "schema_version": 1,
  "id": "kincony_kc868_ap",
  "name": "KinCony KC868-AP",
  "manufacturer": "KinCony",
  "mcu": "esp32",
  "description": "16-channel PWM lighting controller",
  "resources": [
    {
      "id": "i2c0_sda",
      "type": "gpio",
      "gpio": 4,
      "direction": "bidirectional"
    },
    {
      "id": "i2c0_scl",
      "type": "gpio",
      "gpio": 16,
      "direction": "bidirectional"
    },
    {
      "id": "relay1",
      "type": "gpio",
      "gpio": 13,
      "direction": "output",
      "safe_state": false
    },
    {
      "id": "relay2",
      "type": "gpio",
      "gpio": 2,
      "direction": "output",
      "safe_state": false
    },
    {
      "id": "input17",
      "type": "gpio",
      "gpio": 34,
      "direction": "input"
    },
    {
      "id": "input18",
      "type": "gpio",
      "gpio": 35,
      "direction": "input"
    },
    {
      "id": "download_button",
      "type": "gpio",
      "gpio": 0,
      "direction": "input"
    },
    {
      "id": "eth_mdc",
      "type": "gpio",
      "gpio": 23
    },
    {
      "id": "eth_mdio",
      "type": "gpio",
      "gpio": 18
    },
    {
      "id": "eth_clock",
      "type": "gpio",
      "gpio": 17
    },
    {
      "id": "rs485_rx",
      "type": "gpio",
      "gpio": 33
    },
    {
      "id": "rs485_tx",
      "type": "gpio",
      "gpio": 14
    },
    {
      "id": "serial_rx",
      "type": "gpio",
      "gpio": 5
    },
    {
      "id": "serial_tx",
      "type": "gpio",
      "gpio": 32
    }
  ],
  "peripherals": [
    {
      "id": "pca9685_0",
      "type": "pca9685",
      "bus": "i2c0",
      "address": 0,
      "channels": 16,
      "frequency": 1000,
      "settings": {
        "address_verification_required": true,
        "reported_address": "0x00"
      }
    },
    {
      "id": "pcf8574_1",
      "type": "pcf8574",
      "bus": "i2c0",
      "address": 34,
      "channels": 8,
      "settings": {
        "input_range": [1, 8],
        "inverted": true
      }
    },
    {
      "id": "pcf8574_2",
      "type": "pcf8574",
      "bus": "i2c0",
      "address": 33,
      "channels": 8,
      "settings": {
        "input_range": [9, 16],
        "inverted": true
      }
    },
    {
      "id": "ethernet0",
      "type": "ethernet",
      "settings": {
        "phy": "LAN8720",
        "phy_address": 0,
        "power_gpio": -1,
        "mdc_resource": "eth_mdc",
        "mdio_resource": "eth_mdio",
        "clock_mode": "GPIO17_OUT"
      }
    },
    {
      "id": "rs485_0",
      "type": "rs485",
      "settings": {
        "rx_resource": "rs485_rx",
        "tx_resource": "rs485_tx"
      }
    }
  ],
  "capabilities": {
    "external_pwm": true,
    "digital_input": true,
    "relay": true,
    "ethernet": true,
    "rs485": true,
    "i2c": true
  }
}
```

### Reference hardware verification

The supplied PCA9685 address `0x00` is unverified.

The standard PCA9685 default address is `0x40`. The I2C scanner must verify the actual device address before initializing the driver.

Do not automatically configure address `0x40` if the user explicitly supplied another address. Instead, provide a diagnostic and require confirmation.

The KC868-AP I2C bus must be tested against the physical PCB.

Also verify:

- Actual PCF8574 addresses.
- Input polarity.
- Relay polarity.
- PCA9685 output polarity.
- Ethernet clock configuration.
- RS485 transceiver direction behavior.

Do not assume the board's electrical design from the logical pinmap alone.

---

# 7. Hardware Drivers

## 7.1 Required initial drivers

| Driver | Priority |
|---|---|
| GPIO | P0 |
| Native LEDC PWM | P0 |
| I2C Manager | P0 |
| PCA9685 | P0 |
| PCF8574 | P0 |
| Relay | P0 |
| Input Manager | P0 |
| RMT LED | P1 |
| Ethernet | P1 |
| RS485 UART | P1 |
| Generic UART | P1 |

## 7.2 PCA9685

Requirements:

- 16 channels.
- 12-bit resolution.
- Configurable frequency.
- Configurable output polarity.
- Independent channel control.
- Batched updates.
- I2C error recovery.
- Output initialization.
- Frequency validation.

Use a dedicated PCA9685 driver.

Do not let individual lighting channels write directly to the I2C bus.

The PWM service should batch pending changes and avoid redundant I2C transactions.

## 7.3 PCF8574

Requirements:

- Eight inputs per device.
- Multiple devices per I2C bus.
- Configurable active polarity.
- Debouncing.
- Change detection.
- Input event generation.
- Device health monitoring.

## 7.4 Native PWM

Implement ESP-IDF LEDC support.

Requirements:

- Configurable timer frequency.
- Resolution selection.
- Multiple channels.
- Brightness curve.
- Duty cycle control.
- Output inversion.
- Resource allocation.
- Conflict detection.

## 7.5 Addressable LEDs

Implement RMT-based support for:

- WS2812.
- WS2812B.
- SK6812.
- Compatible addressable LED protocols.

Support:

- RGB.
- RGBW.
- LED count.
- Brightness.
- Color.
- Effects.
- Color order.
- Timing configuration.

Addressable LED updates must run independently from the web server and networking.

---

# 8. Lighting Engine

## 8.1 Logical channel model

Every light must be represented as a logical channel.

Example:

```json
{
  "id": "living_room_main",
  "name": "Living Room Main",
  "type": "dimmer",
  "room_id": "living_room",
  "driver": {
    "type": "pca9685",
    "device_id": "pca9685_0",
    "channel": 0
  },
  "state": {
    "power": true,
    "brightness": 75
  },
  "limits": {
    "minimum": 0,
    "maximum": 100
  },
  "transition_ms": 500,
  "startup_behavior": "restore"
}
```

## 8.2 Supported logical types

```text
switch
dimmer
rgb
rgbw
cct
addressable_rgb
addressable_rgbw
```

## 8.3 Internal lighting state

Use a normalized internal representation:

```cpp
struct LightState {
    bool power;
    uint8_t brightness;
    uint8_t red;
    uint8_t green;
    uint8_t blue;
    uint8_t white;
    uint16_t color_temperature;
};
```

Add a separate type-aware validation layer so unsupported properties cannot be accidentally applied to incompatible lights.

The internal representation must distinguish requested state, applied state and actual hardware state.

## 8.4 Color engine

Implement:

- RGB conversion.
- HSV conversion.
- RGBW conversion.
- CCT conversion.
- Gamma correction.
- Brightness compensation.
- Calibration.
- Channel limits.

Brightness and color must be applied consistently across different driver types.

## 8.5 Transition engine

Support:

- Fade in.
- Fade out.
- Brightness transition.
- Color transition.
- CCT transition.
- Scene transition.

Requirements:

- Non-blocking execution.
- Configurable duration.
- Smooth interpolation.
- Cancellation.
- Replacement by newer commands.
- No delay loops inside the lighting task.

## 8.6 Effects engine

Initial effects:

1. Breathing.
2. Pulse.
3. Rainbow.
4. Color cycle.
5. Candlelight.
6. Sunrise.
7. Sunset.
8. Strobe.
9. Custom sequence.

Effects must be resource-managed.

An effect must be cancellable when a user or automation issues a direct lighting command.

---

# 9. Rooms, Groups and Scenes

## 9.1 Room management

Each room contains:

- Room ID.
- Name.
- Floor.
- Icon.
- Lighting channels.
- Groups.
- Sensors.
- Floor-plan references.

## 9.2 Groups

Groups may contain lights from multiple rooms.

Operations:

- Turn on/off.
- Brightness.
- RGB.
- CCT.
- Effect.
- Scene activation.

Define aggregation policies for mixed light types.

For example, changing brightness on a mixed RGB and dimmer group should adjust brightness while preserving each light's color.

## 9.3 Scenes

Scene JSON:

```json
{
  "id": "movie_night",
  "name": "Movie Night",
  "transition_ms": 1500,
  "states": [
    {
      "light_id": "living_room_main",
      "power": true,
      "brightness": 10
    },
    {
      "light_id": "living_room_ambient",
      "power": true,
      "brightness": 30,
      "rgb": {
        "r": 120,
        "g": 70,
        "b": 220
      }
    },
    {
      "light_id": "kitchen_main",
      "power": false
    }
  ]
}
```

Scenes must support:

- Save current state.
- Manual editing.
- Activation.
- Transition.
- Scheduling.
- Automation triggers.
- Duplication.
- Export/import.

---

# 10. SVG Floor-Plan Lighting System

## 10.1 Objective

Build a professional visual floor-plan editor.

The floor plan must become an interactive lighting control interface.

## 10.2 SVG management

Support:

- SVG upload.
- Multiple floors.
- Room layouts.
- Zoom.
- Pan.
- Full screen.
- Background customization.
- Room selection.
- Object placement.

SVG files must be sanitized before rendering.

Remove scripts, event handlers, embedded external resources and unsafe elements.

## 10.3 Overlay architecture

Do not modify the uploaded SVG to store lighting data.

Use:

```text
FloorPlan
   |
   +-- Original SVG
   |
   +-- Room Overlay
   |
   +-- Lighting Overlay
   |
   +-- Sensor Overlay
   |
   +-- Interaction Layer
```

Store object positions independently.

## 10.4 Floor-plan schema

```json
{
  "id": "ground_floor",
  "name": "Ground Floor",
  "svg_file": "ground-floor.svg",
  "view_box": "0 0 1200 800",
  "rooms": [
    {
      "id": "living_room",
      "name": "Living Room",
      "lights": [
        {
          "light_id": "living_room_main",
          "x": 400,
          "y": 250,
          "icon": "ceiling-light",
          "glow_radius": 65,
          "glow_color": "#FFD68A"
        }
      ]
    }
  ]
}
```

## 10.5 Lighting visualization

The frontend must implement:

- Light glow.
- Soft shadows.
- Color overlays.
- Brightness intensity.
- Smooth ON/OFF transitions.
- Color transitions.
- Hover states.
- Selection states.
- Group control.
- Room control.

Use SVG filters and gradients for glow effects.

Rendering is split by mode:

- **Control mode:** inline sanitized SVG with an SVG overlay layer animated by Motion. No canvas.
- **Edit mode:** React Konva stage for drag, select, transform and snap. The sanitized SVG is drawn as a non-interactive background image.

Both modes must use the same SVG `viewBox` coordinate space so saved positions are identical in either mode.

Use CSS transitions for UI feedback.

Do not rely on expensive full-floor-plan raster animations.

## 10.6 Floor-plan editing workflow

1. Upload SVG.
2. Create floor.
3. Identify or create rooms.
4. Add light icons.
5. Assign logical lights.
6. Adjust position.
7. Configure visual properties.
8. Save.
9. Switch to control mode.

Provide separate editing and operating modes.

The ordinary user must not accidentally reposition lights while trying to control them.

---

# 11. Automation Engine

## 11.1 Architecture

Use a trigger-condition-action engine.

```text
EVENT
  |
  v
TRIGGER MATCHER
  |
  v
CONDITION EVALUATOR
  |
  v
ACTION EXECUTOR
  |
  v
EVENT / LOG
```

The engine must run locally.

## 11.2 Trigger types

- Light state change.
- Brightness change.
- Color change.
- Input state change.
- Button press.
- Double press.
- Long press.
- Time schedule.
- Sunrise.
- Sunset.
- Sensor threshold.
- MQTT event.
- ESP-NOW event.
- Scene activated.
- Group state changed.
- Controller startup.
- Timer expired.

## 11.3 Condition operators

- Equal.
- Not equal.
- Greater than.
- Less than.
- Greater than or equal.
- Less than or equal.
- AND.
- OR.
- NOT.
- Time range.
- Day of week.
- State comparison.

## 11.4 Action types

- Light ON.
- Light OFF.
- Set brightness.
- Set color.
- Set CCT.
- Activate scene.
- Control group.
- Toggle relay.
- Run effect.
- Stop effect.
- Delay.
- MQTT publish.
- UDP send.
- TCP send.
- Execute automation.
- Set variable.

## 11.5 Automation JSON

```json
{
  "id": "kitchen_evening",
  "name": "Kitchen Evening Lighting",
  "enabled": true,
  "trigger": {
    "type": "input_state",
    "input_id": "input_01",
    "state": true
  },
  "conditions": {
    "operator": "AND",
    "items": [
      {
        "type": "time_range",
        "start": "18:00",
        "end": "23:00"
      }
    ]
  },
  "actions": [
    {
      "type": "light_power",
      "light_id": "kitchen_main",
      "power": true
    },
    {
      "type": "light_brightness",
      "light_id": "kitchen_main",
      "brightness": 80,
      "transition_ms": 500
    }
  ],
  "execution": {
    "cooldown_ms": 1000,
    "max_executions_per_hour": 100,
    "allow_parallel": false
  }
}
```

## 11.6 Simple editor

Provide a form-based editor.

Users should select:

1. Trigger.
2. Optional conditions.
3. Actions.
4. Execution settings.

## 11.7 Advanced editor

Use React Flow (`@xyflow/react`).

Required node types:

- Trigger.
- Condition.
- AND.
- OR.
- Delay.
- Light action.
- Group action.
- Scene action.
- Relay action.
- MQTT action.
- UDP action.
- TCP action.
- Variable.
- Automation execution.

Connections must be validated.

The visual editor must serialize into a versioned automation graph schema.

The execution engine must not execute arbitrary user-supplied JavaScript.

## 11.8 Execution safety

Implement:

- Execution limits.
- Loop detection.
- Recursive execution prevention.
- Cooldown.
- Cancellation.
- Maximum execution duration.
- Queue limits.
- Error handling.
- Execution history.

An automation failure must not crash the automation service.

---

# 12. Multi-Controller Synchronization

## 12.1 Objective

Support multiple lighting controllers operating as one logical lighting system.

Use two communication methods:

1. ESP-NOW for local low-latency wireless synchronization.
2. MQTT for network-wide synchronization.

## 12.2 Controller roles

Support:

- Standalone controller.
- Primary controller.
- Secondary controller.
- Gateway controller.

A controller may operate independently while also participating in a network.

## 12.3 ESP-NOW architecture

Implement:

- Peer registration.
- Controller identification.
- Device discovery.
- Heartbeat.
- State synchronization.
- Command forwarding.
- Group synchronization.
- Scene synchronization.
- Network status.

ESP-NOW packets must have a versioned binary envelope.

Example logical packet:

```json
{
  "version": 1,
  "message_id": "12345",
  "source": "controller_01",
  "destination": "broadcast",
  "type": "light_state",
  "sequence": 100,
  "timestamp": 1795000000,
  "payload": {
    "light_id": "living_room_main",
    "power": true,
    "brightness": 75
  }
}
```

This JSON is a protocol illustration. Production ESP-NOW packets should use a compact bounded binary representation or another explicitly defined efficient serialization format.

Implement:

- Maximum packet size validation.
- Message authentication.
- Replay protection.
- Sequence numbers.
- Duplicate detection.
- Peer limits.
- Retry policies where appropriate.
- Channel configuration.
- Peer lifecycle management.

Do not assume ESP-NOW supports unlimited peers.

## 12.4 MQTT synchronization

MQTT provides network-wide synchronization.

Topics:

```text
lighting/network/{network_id}/controllers/{controller_id}/status

lighting/network/{network_id}/events

lighting/network/{network_id}/lights/{light_id}/set

lighting/network/{network_id}/lights/{light_id}/state

lighting/network/{network_id}/scenes/{scene_id}/activate
```

## 12.5 Conflict resolution

Use an explicit command ownership and conflict-resolution policy.

Requirements:

- Commands have unique IDs.
- Commands include origin controller.
- Commands include sequence numbers.
- Stale commands are rejected.
- Duplicate commands are ignored.
- Actual state is published after hardware application.
- Local physical input must remain responsive.
- Network loops must be prevented.

Recommended default policy:

- Local physical input commands are processed immediately.
- Valid direct user commands take priority over stale synchronized commands.
- A newer accepted command replaces an active transition.
- State reconciliation must not create endless message loops.

## 12.6 Offline behavior

If MQTT is unavailable:

- ESP-NOW remains active if available.
- Local automations continue.
- Local lighting continues.
- State changes are retained locally.
- MQTT synchronization resumes when connectivity returns.

Do not make lighting functionality dependent on a central controller.

---

# 13. MQTT and Home Assistant

## 13.1 MQTT features

- Configurable broker.
- Username/password.
- TLS.
- Port.
- Client ID.
- QoS.
- Keepalive.
- Retained state.
- LWT.
- Reconnection.
- Topic prefix.
- JSON payloads.
- Event publishing.

## 13.2 Topic convention

```text
lighting/{controller_id}/status

lighting/{controller_id}/light/{light_id}/set
lighting/{controller_id}/light/{light_id}/state

lighting/{controller_id}/group/{group_id}/set

lighting/{controller_id}/scene/{scene_id}/activate

lighting/{controller_id}/event

lighting/{controller_id}/automation/{automation_id}/status
```

## 13.3 Home Assistant Discovery

Support MQTT discovery for:

- Dimmers.
- RGB lights.
- RGBW lights.
- CCT lights.
- Relay lights.
- Supported groups.
- Diagnostics.

Each entity must have a stable unique ID.

Entity configuration must be regenerated when the underlying logical device changes.

Clean up retained discovery configurations when entities are deleted or renamed according to the integration's lifecycle policy.

## 13.4 Home Assistant discovery example

```json
{
  "name": "Living Room Main",
  "unique_id": "controller_01_living_room_main",
  "schema": "json",
  "command_topic": "lighting/controller_01/light/living_room_main/set",
  "state_topic": "lighting/controller_01/light/living_room_main/state",
  "availability_topic": "lighting/controller_01/status",
  "brightness": true
}
```

Use the appropriate supported MQTT light schema and optional properties for each lighting type.

---

# 14. TCP and UDP

## 14.1 TCP

Support:

- TCP server.
- TCP client.
- Persistent connections.
- JSON commands.
- Authentication.
- Heartbeats.
- Connection timeouts.
- Multiple clients.
- Reconnection.

Define a versioned application protocol with explicit message boundaries. Do not assume each TCP read corresponds to one complete JSON message.

## 14.2 UDP

Support:

- Device discovery.
- Broadcast.
- Multicast.
- Lighting events.
- State announcements.
- Low-latency control.

UDP control must be disabled or authenticated by default on untrusted networks.

Implement packet validation and rate limiting.

---

# 15. Network Manager

## 15.1 Wi-Fi station mode

- SSID.
- Password.
- DHCP.
- Static IP.
- Gateway.
- DNS.
- Reconnection.
- Connection status.

## 15.2 Access point mode

- SSID.
- Password.
- Security.
- IP address.
- DHCP server.
- Maximum stations.
- Configuration portal.

Default configuration mode must use a unique device identity and a secure password or explicit provisioning procedure.

Never create an unauthenticated permanent management hotspot.

## 15.3 Combined mode

Support:

- Station only.
- AP only.
- AP + station.
- Fallback AP.

When station connectivity fails, the controller should follow a configurable fallback policy.

## 15.4 Ethernet

Support configurable PHY profiles.

Initial reference:

- LAN8720.
- PHY address 0.
- MDC GPIO23.
- MDIO GPIO18.
- Clock GPIO17 OUT.
- Power GPIO -1.

Ethernet must be initialized only when supported by the selected hardware profile.

---

# 16. Authentication and Security

## 16.1 Initial setup

Default credentials:

```text
Username: admin
Password: admin
```

On first login:

- Force password change.
- Invalidate the default credential.
- Generate secure session keys.
- Require authentication for management APIs.

## 16.2 Password storage

Use a salted password hashing scheme such as PBKDF2-HMAC-SHA256.

Never store plain-text passwords.

Use a cryptographically secure random generator for session tokens.

## 16.3 Session management

- Secure cookies.
- HTTP-only.
- SameSite protection.
- Configurable session timeout.
- Logout.
- Session invalidation after password change.
- Login rate limiting.
- CSRF protection.

## 16.4 Network security

- MQTT TLS.
- Authenticated TCP.
- Authenticated ESP-NOW application messages.
- Secure OTA.
- Input validation.
- API rate limits.
- Request-size limits.
- WebSocket authentication.

ESP-NOW link-layer encryption alone must not be treated as complete application authentication.

## 16.5 Secure storage

Keep sensitive configuration in NVS.

Evaluate ESP-IDF NVS encryption and flash encryption for production deployments.

Do not expose credentials through diagnostics, logs or configuration exports.

---

# 17. Web Server and API

## 17.1 REST API

All API responses must follow a consistent format.

Success:

```json
{
  "success": true,
  "data": {},
  "error": null
}
```

Failure:

```json
{
  "success": false,
  "data": null,
  "error": {
    "code": "INVALID_CONFIGURATION",
    "message": "The selected GPIO is already reserved."
  }
}
```

## 17.2 API endpoints

### Authentication

| Method | Endpoint |
|---|---|
| POST | `/api/v1/auth/login` |
| POST | `/api/v1/auth/logout` |
| GET | `/api/v1/auth/session` |
| PUT | `/api/v1/auth/password` |

### System

| Method | Endpoint |
|---|---|
| GET | `/api/v1/system/info` |
| GET | `/api/v1/system/health` |
| GET | `/api/v1/system/diagnostics` |
| POST | `/api/v1/system/restart` |
| POST | `/api/v1/system/factory-reset` |

### Hardware

| Method | Endpoint |
|---|---|
| GET | `/api/v1/boards` |
| GET | `/api/v1/boards/active` |
| POST | `/api/v1/boards/validate` |
| POST | `/api/v1/boards/apply` |
| POST | `/api/v1/boards` |
| PUT | `/api/v1/boards/{id}` |
| DELETE | `/api/v1/boards/{id}` |
| GET | `/api/v1/hardware/status` |
| GET | `/api/v1/hardware/i2c/scan` |
| GET | `/api/v1/hardware/inputs` |

### Lighting

| Method | Endpoint |
|---|---|
| GET | `/api/v1/lights` |
| POST | `/api/v1/lights` |
| GET | `/api/v1/lights/{id}` |
| PUT | `/api/v1/lights/{id}` |
| DELETE | `/api/v1/lights/{id}` |
| POST | `/api/v1/lights/{id}/command` |

### Rooms and groups

| Method | Endpoint |
|---|---|
| GET | `/api/v1/rooms` |
| POST | `/api/v1/rooms` |
| PUT | `/api/v1/rooms/{id}` |
| DELETE | `/api/v1/rooms/{id}` |
| GET | `/api/v1/groups` |
| POST | `/api/v1/groups` |
| PUT | `/api/v1/groups/{id}` |
| DELETE | `/api/v1/groups/{id}` |

### Scenes

| Method | Endpoint |
|---|---|
| GET | `/api/v1/scenes` |
| POST | `/api/v1/scenes` |
| PUT | `/api/v1/scenes/{id}` |
| DELETE | `/api/v1/scenes/{id}` |
| POST | `/api/v1/scenes/{id}/activate` |

### Floor plans

| Method | Endpoint |
|---|---|
| GET | `/api/v1/floorplans` |
| POST | `/api/v1/floorplans` |
| PUT | `/api/v1/floorplans/{id}` |
| DELETE | `/api/v1/floorplans/{id}` |
| POST | `/api/v1/floorplans/{id}/upload` |
| PUT | `/api/v1/floorplans/{id}/layout` |

### Automations

| Method | Endpoint |
|---|---|
| GET | `/api/v1/automations` |
| POST | `/api/v1/automations` |
| GET | `/api/v1/automations/{id}` |
| PUT | `/api/v1/automations/{id}` |
| DELETE | `/api/v1/automations/{id}` |
| POST | `/api/v1/automations/{id}/enable` |
| POST | `/api/v1/automations/{id}/disable` |
| POST | `/api/v1/automations/{id}/test` |
| GET | `/api/v1/automations/{id}/history` |

### Networking

| Method | Endpoint |
|---|---|
| GET | `/api/v1/network` |
| PUT | `/api/v1/network/wifi` |
| PUT | `/api/v1/network/ap` |
| PUT | `/api/v1/network/ethernet` |
| GET | `/api/v1/network/status` |

### Integrations

| Method | Endpoint |
|---|---|
| GET | `/api/v1/integrations` |
| PUT | `/api/v1/integrations/mqtt` |
| POST | `/api/v1/integrations/mqtt/test` |
| PUT | `/api/v1/integrations/espnow` |
| PUT | `/api/v1/integrations/homeassistant` |
| PUT | `/api/v1/integrations/tcp` |
| PUT | `/api/v1/integrations/udp` |

### OTA and backup

| Method | Endpoint |
|---|---|
| GET | `/api/v1/ota/status` |
| POST | `/api/v1/ota/upload` |
| GET | `/api/v1/backup` |
| POST | `/api/v1/backup/restore` |

## 17.3 WebSocket

Endpoint:

```text
/api/v1/ws
```

Events:

```text
system_status
network_status
light_state_changed
group_state_changed
scene_activated
automation_executed
input_state_changed
hardware_status_changed
controller_discovered
controller_disconnected
```

WebSocket messages must have a versioned envelope.

```json
{
  "version": 1,
  "event": "light_state_changed",
  "timestamp": 1795000000,
  "data": {
    "light_id": "living_room_main",
    "power": true,
    "brightness": 75
  }
}
```

Implement authentication, message validation, bounded queues and reconnect/resubscription behavior.

---

# 18. WebUI / UX Specification

## 18.1 Design direction

The UI must look like a commercial smart lighting application. The full implementation-level design system, component inventory, screen specifications and motion rules are defined in `DESIGN.md`; that document is authoritative for the WebUI.

Design characteristics:

- Minimal.
- Clean.
- Professional.
- Visual.
- Responsive.
- Easy to understand.
- Consistent spacing.
- Clear typography.
- Subtle animations.

Avoid excessive cards, technical terminology, unnecessary charts and crowded dashboards.

Use a light and dark theme.

The interface must prioritize everyday lighting operations over engineering configuration.

## 18.2 Main layout

Desktop:

```text
+--------------------------------------------------------+
| LOGO        Controller Name       Status       Profile |
+-------------+------------------------------------------+
|             |                                          |
| Overview    |                                          |
| Rooms       |             MAIN CONTENT                 |
| Floor Plans |                                          |
| Scenes      |                                          |
| Automations |                                          |
| Devices     |                                          |
| Hardware    |                                          |
| Integrations|                                          |
| Network     |                                          |
| Settings    |                                          |
|             |                                          |
+-------------+------------------------------------------+
```

Mobile:

- Collapsible navigation.
- Bottom navigation for frequently used sections.
- Full-width controls.
- Touch-friendly sliders.
- Responsive floor plans.
- No horizontal scrolling for ordinary screens.

## 18.3 Overview

Display:

- Lighting status.
- Room shortcuts.
- Recently used scenes.
- Active automations.
- Controller connectivity.
- Important system alerts.

Avoid excessive technical metrics on the home page.

## 18.4 Rooms

Each room should have:

- Room name.
- Room image or icon.
- Main power control.
- Brightness control.
- Light count.
- Individual light controls.
- Scene shortcuts.

## 18.5 Light control

Controls must adapt to the lighting type.

Dimmer:

- Power.
- Brightness.

RGB:

- Power.
- Brightness.
- Color picker.

RGBW:

- Power.
- Brightness.
- Color picker.
- White channel.

CCT:

- Power.
- Brightness.
- Warm/cool temperature.

Addressable LEDs:

- Power.
- Brightness.
- Color.
- Effect selection.
- Effect speed.

## 18.6 Floor-plan UI

Provide:

- Floor selector.
- Zoom controls.
- Full-screen control.
- Edit mode.
- Control mode.
- Light placement.
- Glow settings.
- Room selection.
- Live state.

Do not show engineering properties in ordinary operating mode.

## 18.7 Scenes UI

Use visual scene cards.

Each card should contain:

- Scene name.
- Representative icon or image.
- Activation button.
- Optional edit action.

## 18.8 Automation UI

Two tabs:

- Simple.
- Advanced.

Simple mode must be usable by nontechnical users.

Advanced mode must provide a node editor with searchable nodes and clear connections.

## 18.9 Hardware UI

Provide separate tabs:

- Board.
- Pin Mapping.
- Peripherals.
- Outputs.
- Inputs.
- Diagnostics.

Use graphical GPIO assignment and conflict indicators.

## 18.10 Design tokens

Design tokens are defined as CSS variables and mapped into the Tailwind theme (`@theme` in Tailwind v4, or `theme.extend` in v3), following the shadcn/ui theming convention (`--background`, `--foreground`, `--primary`, `--muted`, `--border`, `--ring`, `--destructive`, plus `--success` and `--warning`).

Full token values for light and dark themes are in `DESIGN.md` section 4.

Do not hardcode colors, radii or spacing values in individual components.

---

# 19. Data Storage

## 19.1 Storage responsibilities

| Storage | Purpose |
|---|---|
| NVS | Credentials and critical settings |
| LittleFS | JSON configuration and static assets |
| RAM | Runtime states and active execution |
| OTA partitions | Firmware images |

## 19.2 File structure

```text
/config/
    system.json
    active_board.json

    /boards/
        custom_01.json

    /lights/
        channels.json

    /rooms/
        rooms.json

    /groups/
        groups.json

    /scenes/
        scenes.json

    /automations/
        kitchen_evening.json

    /floorplans/
        ground_floor.json

    /integrations/
        mqtt.json
        espnow.json

    /backups/
        backup_manifest.json
```

Sensitive credentials must not be stored in ordinary JSON configuration files.

## 19.3 Configuration versioning

Every persistent document must have:

- Schema version.
- Configuration version.
- Validation.
- Migration logic.
- Backup.
- Recovery.

A firmware upgrade must not silently discard user configuration.

## 19.4 Configuration backup

Provide:

- Full backup.
- Partial backup.
- Restore.
- Version compatibility checks.
- Import validation.
- Restore confirmation.

Exclude sensitive credentials by default from portable exports, with an explicit secure export option if supported.

---

# 20. OTA and Firmware Recovery

Implement:

- Dual OTA application partitions.
- Firmware version.
- Firmware metadata.
- Upload progress.
- Image validation.
- Partition validation.
- Compatibility validation.
- Rollback.
- Configuration preservation.

Never activate an image that fails validation.

Production releases should be signed and verified.

A firmware update must not depend on the WebUI remaining connected throughout the update process.

---

# 21. Logging and Diagnostics

Implement structured logs.

Log categories:

- SYSTEM.
- HARDWARE.
- LIGHTING.
- AUTOMATION.
- NETWORK.
- MQTT.
- ESP_NOW.
- WEB.
- OTA.
- SECURITY.

Diagnostics page:

- Firmware version.
- MCU information.
- Free heap.
- Minimum free heap.
- CPU/task statistics where available.
- Network state.
- I2C devices.
- Hardware health.
- Active lighting channels.
- Active automations.
- Controller peers.
- Recent errors.

Avoid logging passwords, access tokens or sensitive payloads.

---

# 22. Firmware Source Structure

```text
smart-lighting-controller/
│
├── platformio.ini
├── sdkconfig.defaults
├── partitions.csv
├── CMakeLists.txt
├── AGENTS.md
├── README.md
│
├── include/
│
├── src/
│   ├── main.cpp
│   │
│   ├── core/
│   │   ├── system_manager.cpp
│   │   ├── event_bus.cpp
│   │   ├── config_manager.cpp
│   │   ├── storage_manager.cpp
│   │   ├── device_registry.cpp
│   │   └── diagnostics.cpp
│   │
│   ├── hardware/
│   │   ├── hardware_manager.cpp
│   │   ├── board_profile.cpp
│   │   ├── pin_validator.cpp
│   │   ├── gpio_driver.cpp
│   │   ├── ledc_driver.cpp
│   │   ├── i2c_manager.cpp
│   │   ├── pca9685_driver.cpp
│   │   ├── pcf8574_driver.cpp
│   │   ├── relay_driver.cpp
│   │   ├── rmt_led_driver.cpp
│   │   ├── ethernet_driver.cpp
│   │   └── rs485_driver.cpp
│   │
│   ├── lighting/
│   │   ├── lighting_engine.cpp
│   │   ├── channel_manager.cpp
│   │   ├── color_engine.cpp
│   │   ├── transition_engine.cpp
│   │   ├── effects_engine.cpp
│   │   ├── scene_manager.cpp
│   │   ├── group_manager.cpp
│   │   └── room_manager.cpp
│   │
│   ├── automation/
│   │   ├── rule_engine.cpp
│   │   ├── trigger_manager.cpp
│   │   ├── condition_evaluator.cpp
│   │   ├── action_executor.cpp
│   │   ├── scheduler.cpp
│   │   └── automation_logger.cpp
│   │
│   ├── networking/
│   │   ├── wifi_manager.cpp
│   │   ├── mqtt_manager.cpp
│   │   ├── ha_discovery.cpp
│   │   ├── espnow_manager.cpp
│   │   ├── tcp_server.cpp
│   │   ├── udp_manager.cpp
│   │   └── network_manager.cpp
│   │
│   ├── web/
│   │   ├── web_server.cpp
│   │   ├── api_routes.cpp
│   │   ├── websocket_manager.cpp
│   │   ├── auth_manager.cpp
│   │   └── ota_manager.cpp
│   │
│   └── services/
│       ├── input_manager.cpp
│       ├── button_manager.cpp
│       ├── time_manager.cpp
│       └── health_monitor.cpp
│
├── boards/
│   ├── kincony_kc868_ap.json
│   ├── generic_esp32.json
│   └── generic_esp32s3.json
│
├── schemas/
│   ├── board-profile.schema.json
│   ├── light.schema.json
│   ├── room.schema.json
│   ├── group.schema.json
│   ├── scene.schema.json
│   ├── automation.schema.json
│   └── floorplan.schema.json
│
├── webui/
│   ├── package.json
│   ├── components.json          # shadcn/ui config
│   ├── tsconfig.json
│   ├── vite.config.ts
│   └── src/
│       ├── app/                 # router, providers, layout
│       ├── components/
│       │   ├── ui/              # shadcn/ui primitives
│       │   └── lighting/        # LightCard, BrightnessSlider, ColorPicker...
│       ├── features/
│       │   ├── overview/
│       │   ├── rooms/
│       │   ├── floorplan/       # editor (Konva) + viewer (SVG + Motion)
│       │   ├── scenes/
│       │   ├── automations/     # simple form + React Flow editor
│       │   ├── hardware/
│       │   ├── integrations/
│       │   └── settings/
│       ├── hooks/
│       ├── lib/                 # utils, cn(), sanitizer
│       ├── schemas/             # Zod schemas
│       ├── services/            # typed API client, WebSocket client
│       ├── stores/              # Zustand
│       ├── types/
│       └── styles/              # globals.css, tokens
│
├── test/
│   ├── unit/
│   ├── integration/
│   ├── hardware/
│   └── fixtures/
│
└── docs/
    ├── ARCHITECTURE.md
    ├── API.md
    ├── HARDWARE_PROFILES.md
    ├── AUTOMATION.md
    ├── ESP_NOW_PROTOCOL.md
    └── UI_UX.md
```

Each module should have a public header and implementation file where appropriate.

The tree is the target architecture. The coding agent may adjust file boundaries when necessary, but must preserve the architectural separation.

---

# 23. FreeRTOS Task Architecture

Use bounded queues and event-driven communication.

Logical services:

| Service | Responsibility |
|---|---|
| Lighting service | Hardware output control |
| Input service | Physical input monitoring |
| Automation service | Rule execution |
| Network service | Wi-Fi and Ethernet |
| MQTT service | Broker communication |
| ESP-NOW service | Wireless synchronization |
| Web service | HTTP and WebSocket |
| Scheduler | Time-based events |
| Diagnostics | System monitoring |

Do not automatically create one FreeRTOS task per service.

Share event loops and task contexts where appropriate to conserve RAM.

Rules:

1. Networking callbacks must not block on hardware operations.
2. Lighting commands must use a bounded command queue.
3. Long-running actions must be asynchronous.
4. Critical events must not be silently dropped.
5. Low-priority telemetry may be coalesced.
6. Queue overflow must be observable.
7. Task stack sizes must be measured.
8. Watchdog monitoring must be implemented.
9. All task ownership and shutdown behavior must be documented.

---

# 24. Performance and Reliability Requirements

The following are initial engineering targets, subject to measurement on each supported board.

| Metric | Target |
|---|---|
| Local lighting command processing | Under 100 ms |
| UI state propagation | Under 250 ms on healthy LAN |
| PWM transition smoothness | At least 30 updates/sec where hardware permits |
| Automation trigger processing | Under 100 ms excluding intentional delays |
| Startup recovery | No configuration loss |
| Network reconnection | Automatic |
| Runtime | Stable continuous operation |
| Configuration writes | Transactional |
| OTA | Rollback-capable |

Additional requirements:

- Avoid unnecessary heap allocations in frequent paths.
- Avoid excessive JSON parsing.
- Bound all network message sizes.
- Avoid blocking I2C transactions in high-priority tasks.
- Monitor heap fragmentation.
- Avoid storing frequently changing runtime state in flash.
- Use static allocation for critical RTOS resources where practical.

---

# 25. Testing Strategy

## 25.1 Unit tests

Test:

- Pin validator.
- Board profile parser.
- Configuration migration.
- Color conversion.
- Brightness mapping.
- Transition engine.
- Group aggregation.
- Scene application.
- Automation conditions.
- Trigger matching.
- Command serialization.
- ESP-NOW message validation.

Use host-based unit tests for logic that does not require ESP-IDF hardware APIs.

## 25.2 Hardware tests

### KC868-AP

Test:

- I2C initialization.
- PCA9685 discovery.
- All 16 PWM outputs.
- Both PCF8574 devices.
- All 16 expanded digital inputs.
- GPIO34 and GPIO35.
- Both relays.
- Ethernet.
- RS485.
- Download button.

### Generic ESP32

Test:

- Native PWM.
- GPIO input/output.
- RMT.
- Wi-Fi.
- ESP-NOW.
- I2C expansion.

### ESP32-S3

Test:

- GPIO differences.
- Native PWM.
- RMT.
- Wi-Fi.
- ESP-NOW.
- PSRAM configuration where present.

## 25.3 Integration tests

- REST login.
- Password change.
- Lighting command.
- WebSocket state propagation.
- MQTT command.
- MQTT state publication.
- Home Assistant discovery.
- ESP-NOW peer communication.
- TCP client/server.
- UDP discovery.
- Wi-Fi fallback.
- Ethernet initialization.
- OTA update.
- Configuration rollback.

## 25.4 Automation tests

- Input trigger.
- Time trigger.
- Multiple conditions.
- AND/OR.
- Delay.
- Scene action.
- Group action.
- Recursive execution prevention.
- Cooldown.
- Failure handling.
- Persistence after reboot.

## 25.5 Frontend tests

Use:

- Vitest.
- React Testing Library.
- Playwright.
- Mock Service Worker for API mocking.
- Axe accessibility checks.

Test:

- Authentication.
- Dashboard.
- Lighting controls.
- Room management.
- Scene management.
- Floor-plan editing.
- Floor-plan control mode.
- Automation simple editor.
- Automation node editor.
- Hardware configuration.
- Mobile responsiveness.
- Network configuration.

## 25.6 Reliability tests

- MQTT disconnection.
- Wi-Fi disconnection.
- ESP-NOW peer loss.
- I2C peripheral failure.
- Invalid configuration.
- Corrupted configuration file.
- Queue overflow.
- Low-memory conditions.
- Repeated light commands.
- Long-duration transitions.
- OTA interruption.

## 25.7 Hardware safety testing

Before connecting real lighting loads:

1. Verify output polarity.
2. Verify boot states.
3. Verify relay behavior.
4. Verify PWM duty.
5. Test with suitable low-voltage test loads.
6. Verify thermal behavior.
7. Verify maximum channel current according to board specifications.
8. Verify power-failure recovery.

Do not use mains-connected loads during initial firmware validation.

---

# 26. Development Phases

The coding agent must implement the project in the following order.

## Phase 0 — Repository and project foundation

Tasks:

- Create PlatformIO ESP-IDF project.
- Create React/Vite project.
- Configure build environments.
- Create source structure.
- Configure formatting and linting.
- Configure unit tests.
- Create AGENTS.md.
- Create SKILL.md.
- Create architecture documentation.

Acceptance:

- Firmware compiles.
- Frontend builds.
- Project structure is documented.
- CI build checks pass.

## Phase 1 — Core firmware

Implement:

- System manager.
- Event bus.
- Storage manager.
- Configuration manager.
- Logging.
- Diagnostics.
- FreeRTOS service lifecycle.

Acceptance:

- Device boots reliably.
- Configuration survives reboot.
- Invalid configuration is rejected.
- Services start and stop cleanly.

## Phase 2 — Hardware abstraction

Implement:

- GPIO.
- I2C.
- LEDC.
- PCA9685.
- PCF8574.
- Relay.
- Pin validation.
- Board profile loader.

Acceptance:

- Generic ESP32 works.
- KC868-AP profile loads.
- Pin conflicts are detected.
- Peripheral diagnostics work.

## Phase 3 — KC868-AP hardware

Implement and validate:

- 16 PWM channels.
- 16 expanded inputs.
- Two direct inputs.
- Two relays.
- Ethernet.
- RS485.

Acceptance:

- Physical hardware tests pass.
- All resources are correctly mapped.
- I2C diagnostics report actual devices.

## Phase 4 — Lighting engine

Implement:

- Logical channels.
- Native PWM.
- PCA9685 PWM.
- RGB/RGBW.
- CCT.
- Relay.
- Addressable LEDs.
- Color engine.
- Transitions.
- Effects.

Acceptance:

- All supported lighting types work.
- Commands are hardware-independent.
- Transitions are non-blocking.

## Phase 5 — Rooms, groups and scenes

Implement:

- Room manager.
- Group manager.
- Scene manager.
- Persistence.
- Group aggregation.

Acceptance:

- Lights can be organized.
- Groups operate correctly.
- Scenes restore saved lighting states.

## Phase 6 — WebUI foundation

Implement:

- Authentication.
- Main layout.
- Dashboard.
- Rooms.
- Devices.
- Lighting controls.
- REST integration.
- WebSocket integration.
- Dark/light theme.

Acceptance:

- Desktop and mobile UI function.
- Lighting state updates in real time.

## Phase 7 — Hardware configuration UI

Implement:

- Board selection.
- Custom profile editor.
- GPIO assignment.
- Peripheral editor.
- Validation.
- Import/export.
- Hardware diagnostics.

Acceptance:

- User can create and save a custom board profile.
- Pin conflicts are shown.
- Failed configuration can be rolled back.

## Phase 8 — Floor-plan editor

Implement:

- SVG upload.
- SVG sanitization.
- Floor management.
- Room mapping.
- Lighting placement.
- Glow rendering.
- Control/edit modes.
- Real-time synchronization.

Acceptance:

- Floor plans can be uploaded and saved.
- Lights can be positioned.
- Visual state follows actual hardware state.

## Phase 9 — Automation engine

Implement:

- Trigger manager.
- Condition evaluator.
- Action executor.
- Scheduler.
- Automation persistence.
- Execution history.
- Simple editor.
- React Flow editor.

Acceptance:

- Automations run offline.
- Both editors produce valid rules.
- Loop prevention works.
- Execution logs are available.

## Phase 10 — Networking

Implement:

- Wi-Fi station.
- AP.
- Ethernet.
- MQTT.
- Home Assistant Discovery.
- TCP.
- UDP.
- RS485 integration.

Acceptance:

- All protocols work independently.
- Network failures do not stop local lighting.

## Phase 11 — Multi-controller networking

Implement:

- ESP-NOW.
- Peer registration.
- Discovery.
- Heartbeat.
- Message validation.
- State synchronization.
- MQTT synchronization.
- Conflict resolution.

Acceptance:

- Two or more supported controllers synchronize lighting.
- MQTT outage does not disable ESP-NOW.
- Offline local operations continue.
- Duplicate commands and synchronization loops are prevented.

## Phase 12 — Security and production

Implement:

- Authentication hardening.
- Secure storage.
- OTA.
- Rollback.
- Backup.
- Recovery.
- Diagnostics.
- Performance optimization.

Acceptance:

- Security tests pass.
- OTA recovery works.
- Configuration survives firmware updates.
- Long-duration reliability tests pass.

---

# 27. AGENTS.md

Create the following file in the repository root.

```markdown
# AGENTS.md

# Project Rules for AI Coding Agents

## 1. Project Objective

Build Lumen, a universal ESP32 smart lighting controller using:

- ESP-IDF
- PlatformIO
- C++17
- FreeRTOS
- React
- TypeScript
- Vite

The KinCony KC868-AP is the first reference board.

The application must remain hardware-independent.

## 2. Mandatory Architecture Rules

1. Never hardcode board-specific GPIO numbers in application modules.
2. All hardware access must go through HAL interfaces.
3. All board configurations must use versioned board profiles.
4. Validate pin ownership before applying configurations.
5. Keep lighting control independent of networking.
6. Keep automation execution local.
7. Do not block lighting operations with HTTP, MQTT or WebSocket callbacks.
8. Use the internal event bus for subsystem communication.
9. Keep persistence operations transactional.
10. Preserve user configuration across firmware upgrades.

## 3. Firmware Rules

- Use ESP-IDF APIs.
- Do not use Arduino framework dependencies.
- Use C++17.
- Use RAII where appropriate.
- Check ESP-IDF return values.
- Use bounded queues.
- Avoid blocking operations in critical tasks.
- Avoid unnecessary dynamic memory allocation.
- Do not create uncontrolled FreeRTOS tasks.
- Document task priorities and stack sizes.
- Handle hardware initialization failures.
- Never silently ignore errors.

## 4. Hardware Rules

- GPIO assignments come from board profiles.
- Check input/output capability.
- Check boot-strapping restrictions.
- Check peripheral conflicts.
- Check reserved flash and PSRAM pins.
- Verify external peripheral addresses.
- Keep drivers independent of application logic.
- Do not assume that MCU capability means board-level availability.

## 5. Lighting Rules

- Use logical lighting channels.
- Support PWM, relay, RGB, RGBW, CCT and addressable LEDs.
- Use normalized lighting state.
- Use a dedicated color engine.
- Use non-blocking transitions.
- Avoid unnecessary hardware writes.
- Maintain requested and applied state separately.

## 6. Automation Rules

- Automations must run locally.
- Use trigger-condition-action architecture.
- Validate every automation before activation.
- Prevent recursion and execution loops.
- Bound execution queues.
- Support cancellation.
- Never execute arbitrary JavaScript or shell commands.
- Log execution results.

## 7. Networking Rules

- Network callbacks enqueue commands.
- Do not execute slow hardware operations inside callbacks.
- Validate all received packets.
- Use protocol versioning.
- Use authentication for control operations.
- Implement reconnection.
- Support offline operation.
- Never expose credentials in logs.

## 8. Frontend Rules

- Use React, TypeScript, Vite, shadcn/ui and Tailwind CSS.
- Follow `DESIGN.md`.
- Use functional components.
- Use TanStack Query for server state and Zustand for live/UI state.
- Use Zod schemas with React Hook Form.
- Use Sonner for notifications.
- Lazy-load Konva, React Flow, Recharts and GSAP.
- Use reusable components.
- Use shared design tokens.
- Use typed API clients.
- Validate forms.
- Handle API errors.
- Support desktop and mobile.
- Avoid unnecessary UI complexity.
- Keep the visual interface professional and consistent.

## 9. Floor-Plan Rules

- Use sanitized SVG.
- Use React Konva only in edit mode; control mode uses SVG + Motion.
- Keep original SVG separate from overlay configuration.
- Store positions in normalized or SVG coordinate space.
- Separate edit and operating modes.
- Use actual device state for visualization.
- Do not rely on browser-only simulated lighting state.

## 10. Testing Rules

Every feature must include relevant tests.

Before completing a phase:

1. Compile firmware.
2. Run unit tests.
3. Build frontend.
4. Run frontend tests.
5. Validate schemas.
6. Review memory usage.
7. Update documentation.

Hardware-dependent features must have mock drivers for host tests.

## 11. Documentation Rules

Update relevant documentation whenever:

- API contracts change.
- JSON schemas change.
- Hardware profiles change.
- Protocols change.
- Configuration migration changes.
- UI workflows change.

## 12. Coding Workflow

For every task:

1. Inspect existing architecture.
2. Identify affected modules.
3. Read relevant documentation.
4. Prepare an implementation plan.
5. Implement the smallest complete feature.
6. Add tests.
7. Build and validate.
8. Update documentation.
9. Report completed work and remaining limitations.

Do not mark a feature complete merely because code has been written.

## 13. Prohibited Shortcuts

- No hardcoded KC868-AP GPIO assignments outside its board profile.
- No fake hardware state.
- No placeholder APIs marked as implemented.
- No empty implementation files presented as completed modules.
- No arbitrary pin allocation.
- No blocking delays in lighting control.
- No insecure default production credentials.
- No destructive configuration migration.
- No silent error suppression.
- No unnecessary dependencies.

## 14. Completion Criteria

A feature is complete only when:

- Implementation exists.
- Validation exists.
- Tests exist.
- Build passes.
- Documentation is updated.
- Known limitations are documented.
```

---

# 28. SKILL.md

Create a reusable coding-agent skill.

```markdown
# SKILL.md

# Lumen Development Skill

## Purpose

Guide AI coding agents implementing Lumen.

## When to Use

Use this skill when working on:

- ESP-IDF firmware.
- PlatformIO.
- Hardware abstraction.
- ESP32 board profiles.
- PWM drivers.
- PCA9685.
- PCF8574.
- Lighting engines.
- RGB and RGBW.
- CCT.
- Addressable LEDs.
- Automation.
- ESP-NOW.
- MQTT.
- Home Assistant.
- React WebUI.
- SVG floor plans.

## Development Procedure

### Step 1: Understand the subsystem

Read:

- AGENTS.md
- ARCHITECTURE.md
- Relevant module documentation
- Relevant hardware profiles
- Relevant schemas

Do not implement a subsystem without understanding its dependencies.

### Step 2: Define interfaces

Identify:

- Public interfaces.
- Input data.
- Output data.
- Events.
- Errors.
- Persistent state.
- Resource ownership.

Keep interfaces stable and explicit.

### Step 3: Implement

Implement the feature using existing project patterns.

Do not introduce unnecessary abstractions or dependencies.

### Step 4: Validate

Run:

- Compilation.
- Unit tests.
- Schema validation.
- Integration tests where possible.
- Static analysis.

### Step 5: Document

Update:

- API documentation.
- Architecture documentation.
- Hardware documentation.
- Tests.
- Examples.

## Hardware Development Guidelines

Before using a GPIO:

1. Check board profile.
2. Check MCU capability.
3. Check reserved pins.
4. Check conflicts.
5. Check electrical polarity.
6. Check safe startup state.

Never guess physical pin mappings.

## Driver Development Guidelines

Drivers must:

- Have explicit initialization.
- Report errors.
- Support shutdown where applicable.
- Validate input ranges.
- Avoid unnecessary blocking.
- Be independently testable.
- Avoid direct application dependencies.

## Lighting Development Guidelines

All lighting operations must use the logical lighting engine.

Never allow the frontend or MQTT layer to manipulate GPIO directly.

## Automation Guidelines

Use event-driven execution.

Never block the main lighting task with delays.

Prevent recursion and excessive execution.

## Networking Guidelines

Validate all incoming messages.

Use bounded buffers.

Implement protocol versioning.

Handle disconnects.

## Frontend Guidelines

Use TypeScript types for API payloads.

Keep API services separate from UI components.

Use shared components and design tokens.

Make every interactive control functional.

## Completion Report

At the end of a task, report:

- Implemented functionality.
- Modified modules.
- Tests executed.
- Build results.
- Hardware validation status.
- Remaining issues.
```

---

# 29. Coding-Agent Execution Instructions

The AI coding agent must work sequentially.

## Rule 1: Never implement everything in one pass

Work phase by phase.

## Rule 2: Build before expanding

Every phase must compile before moving to the next phase.

## Rule 3: Maintain a working repository

Do not leave the project in an uncompilable state between milestones.

## Rule 4: Hardware-independent testing

Use mock implementations for hardware drivers so the majority of firmware logic can be tested without a physical board.

## Rule 5: Real hardware verification

A mock test does not establish that a physical GPIO, peripheral or output works.

Mark physical hardware tests separately.

## Rule 6: Maintain implementation tracking

Create:

`docs/IMPLEMENTATION_STATUS.md`

Use:

```markdown
# Implementation Status

## Phase 0
- [ ] PlatformIO project
- [ ] React project
- [ ] Build configuration

## Phase 1
- [ ] System manager
- [ ] Event bus
- [ ] Storage manager

## Phase 2
- [ ] HAL
- [ ] Board profiles
- [ ] Pin validator

## Phase 3
- [ ] KC868-AP drivers

## Phase 4
- [ ] Lighting engine

## Phase 5
- [ ] Rooms
- [ ] Groups
- [ ] Scenes

## Phase 6
- [ ] WebUI

## Phase 7
- [ ] Hardware editor

## Phase 8
- [ ] Floor-plan editor

## Phase 9
- [ ] Automation

## Phase 10
- [ ] MQTT
- [ ] Home Assistant
- [ ] TCP
- [ ] UDP

## Phase 11
- [ ] ESP-NOW
- [ ] Multi-controller synchronization

## Phase 12
- [ ] OTA
- [ ] Security
- [ ] Production validation
```

## Rule 7: Do not fake completed functionality

A page with placeholder data is not a completed feature.

A driver that compiles but has never communicated with its peripheral must not be marked hardware-verified.

---

# 30. Definition of Done

The first production-ready release must meet the following criteria.

## Hardware

- [ ] KC868-AP preset.
- [ ] Generic ESP32 preset.
- [ ] Generic ESP32-S3 preset.
- [ ] Custom board editor.
- [ ] Pin conflict detection.
- [ ] PCA9685.
- [ ] PCF8574.
- [ ] Native PWM.
- [ ] Relay.
- [ ] RMT addressable LEDs.
- [ ] Ethernet.
- [ ] RS485.

## Lighting

- [ ] Single-channel dimming.
- [ ] Relay lighting.
- [ ] RGB.
- [ ] RGBW.
- [ ] CCT.
- [ ] Addressable LEDs.
- [ ] Effects.
- [ ] Transitions.
- [ ] Rooms.
- [ ] Groups.
- [ ] Scenes.

## WebUI

- [ ] Authentication.
- [ ] Dashboard.
- [ ] Room controls.
- [ ] Light controls.
- [ ] Scene controls.
- [ ] Floor-plan editor.
- [ ] Responsive interface.
- [ ] Dark theme.
- [ ] Light theme.

## Automation

- [ ] Trigger engine.
- [ ] Condition engine.
- [ ] Action engine.
- [ ] Simple editor.
- [ ] Advanced visual editor.
- [ ] Scheduler.
- [ ] Execution history.
- [ ] Loop prevention.

## Networking

- [ ] Wi-Fi station.
- [ ] Wi-Fi AP.
- [ ] Ethernet.
- [ ] MQTT.
- [ ] Home Assistant Discovery.
- [ ] TCP.
- [ ] UDP.
- [ ] ESP-NOW.
- [ ] Multi-controller synchronization.

## Security and Reliability

- [ ] Default account provisioning.
- [ ] Password change.
- [ ] Secure sessions.
- [ ] Secure configuration.
- [ ] OTA.
- [ ] Rollback.
- [ ] Backup and restore.
- [ ] Watchdog.
- [ ] Diagnostics.
- [ ] Long-duration testing.

---

# 31. Future Expansion

The architecture should leave room for:

- Modbus lighting equipment.
- DALI lighting gateways.
- DMX512.
- Zigbee lighting gateways.
- Matter integration.
- Ambient light sensors.
- Presence detection.
- Daylight harvesting.
- Circadian lighting.
- Energy monitoring.
- Mobile applications.
- Remote management.
- Centralized multi-building management.
- Cloud synchronization.

These are future extensions, not requirements for the first release.

New protocols must be added through independent adapters.

---

# 32. Reference Documentation

Use primary documentation when implementing drivers and protocols.

### KinCony

- KC868-AP product and hardware documentation: https://www.kincony.com/esp32-pwm-dimmer.html
- KC868-AP schematic: https://www.kincony.com/download/KC868-AP-schematic.pdf
- KC868-AP pin definitions: https://www.kincony.com/forum/showthread.php?pid=5308&tid=2475

### ESP-IDF

- ESP-IDF documentation: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/
- HTTP Server: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/protocols/esp_http_server.html
- ESP-NOW: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/network/esp_now.html
- LEDC: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/ledc.html
- RMT: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/rmt.html
- OTA: https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/ota.html

### Frontend

- React: https://react.dev/
- TypeScript: https://www.typescriptlang.org/docs/
- Vite: https://vite.dev/
- React Flow: https://reactflow.dev/
- SVG: https://developer.mozilla.org/en-US/docs/Web/SVG

### Protocols

- MQTT: https://mqtt.org/
- Home Assistant MQTT Discovery: https://www.home-assistant.io/integrations/mqtt/
- ESP-NOW: Espressif ESP-IDF documentation.
- PCA9685: NXP PCA9685 datasheet.
- PCF8574: NXP PCF8574 datasheet.

---

# 33. Final Implementation Directive

Build this project as a complete, reusable smart lighting firmware platform.

The KinCony KC868-AP must be the first reference implementation, but no application-level module may depend directly on its pinmap or hardware configuration.

The system must provide:

1. Universal hardware profiles.
2. Independent lighting engine.
3. Multiple lighting technologies.
4. Room and scene management.
5. Visual floor-plan lighting control.
6. Local automation.
7. ESP-NOW and MQTT synchronization.
8. Home Assistant integration.
9. TCP and UDP communication.
10. Professional responsive WebUI.
11. Secure configuration and OTA.
12. Comprehensive testing.

**The most important architectural requirement: a user must be able to change the physical ESP32 board, create a new hardware profile, map the hardware resources and continue using the same lighting application, automation engine, floor plans, scenes and networking integrations.**

Implement the platform in the prescribed phases. Maintain documentation, tests and a working build throughout development.

**End of specification.**