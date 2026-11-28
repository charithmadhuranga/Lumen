# intro

Welcome to this series on Lumen, a universal smart lighting controller for ESP32 boards.

Lumen is open source firmware with a complete web app built in. You flash it onto an ESP32, open a browser, and control every light in your home from the controller itself.

There is no cloud account and no internet connection involved. Lights, rooms, scenes, floor plans and automations all run locally on the board.

Lumen drives every common light type. Simple switches, dimmers, RGB and RGBW strips, and tunable white lights that move between warm and cool.

It works with many boards. The reference board is the KinCony KC868-AP, with sixteen PWM outputs, two relays and eighteen inputs. A plain ESP32 DevKit works too.

If you use Home Assistant, Lumen appears there automatically through MQTT discovery. And if you have more than one controller, they find each other over ESP-NOW, without a router in between.

In this series we start with the architecture and the hardware. Then we flash a board, add lights, build rooms and scenes, and draw a floor plan of the home.

After that we create automations, look at the hardware tools, connect Home Assistant, link two controllers, and finish with networking, security and safe updates.

Everything you will see is running on real hardware: a KC868-AP and a generic ESP32 DevKit, side by side on my desk.

Let's get started.
