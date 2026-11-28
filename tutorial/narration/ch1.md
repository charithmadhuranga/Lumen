# ch1

Before we flash anything, it helps to understand how Lumen is put together.

Lumen has two halves. The firmware runs on the ESP32 and owns the hardware. The web app is a React application that the firmware serves from its own flash memory.

Inside the firmware, one rule shapes everything: lighting control never depends on the network.

A single lighting task owns the outputs. HTTP, WebSocket, MQTT and ESP-NOW never touch the hardware directly. They only place commands in a queue, and the lighting task applies them.

That means a slow Wi-Fi network, a broker that goes offline, or a busy web page can never stall a fade or freeze a light.

Subsystems talk to each other through an internal event bus. When a light changes, an event goes out, and the web app, MQTT and the automation engine each react to it in their own time.

The second rule is that application code never knows a GPIO number.

All hardware access goes through a hardware abstraction layer. Which pin drives which output is described in a board profile: a versioned JSON document.

Before a profile is applied, a pin validator checks it. It rejects a pin used twice, a flash pin, an input-only pin used as an output, and it warns about strapping pins that affect boot.

Lumen ships built-in profiles for the KinCony KC868-AP, for a generic ESP32 DevKit, and for generic boards with a PCA9685 PWM chip, a PCF8574 expander, or native PWM only.

For anything else, you duplicate a profile in the web app and edit the pins. The firmware itself stays exactly the same.

Lights in Lumen are logical channels. A light has a type, such as dimmer or RGB, and it maps to one or more hardware outputs.

The firmware keeps three views of every light: the state you requested, the value actually applied, and what the hardware reports. The web app always shows real state, never a guess.

Storage is split in two. Secrets such as passwords and keys live in NVS, the ESP32's key-value storage. Configuration lives as JSON files on a LittleFS partition.

Every write is transactional. If saving a file fails half way, the last working configuration is kept, and upgrades never throw your settings away.

With that picture in mind, let's put the firmware on a board.
