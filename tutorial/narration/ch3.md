# ch3

The Devices page lists every light the controller drives, with its type, its hardware output and its room.

A light in Lumen is one of five types. A switch is simply on or off. A dimmer adds brightness. RGB adds colour, RGBW adds a separate white channel, and tunable white mixes a warm and a cool channel.

Each type needs a matching number of outputs. A dimmer uses one PWM channel, RGB uses three consecutive channels, RGBW uses four, and tunable white uses two.

To add a light, open the Add light dialog and give it a name. Lumen suggests an ID from the name, which automations, MQTT and the API use to refer to it.

Choose the type, then pick a hardware output. The list only offers outputs that are free, so the same channel can never be given to two lights.

On the KC868-AP, the outputs are the sixteen channels of its PCA9685 PWM chip and the two relays. On a DevKit, they are native LEDC PWM pins from its profile.

If you pick a type that a single output cannot drive, such as RGB on one LEDC pin, the firmware refuses the configuration and tells you why.

You can assign a room straight away, set a default fade time, and choose what the light does at power-up: restore its last state, start off, or start on.

As soon as you save, the light exists everywhere: in rooms, scenes, automations, the floor plan, MQTT and Home Assistant. There is no restart.

Brightness in Lumen is perceptual. Fifty percent looks like half as bright, so the firmware applies a curve before it sets the PWM duty cycle.

Every change fades smoothly, and fades never block. If a new command arrives during a fade, it simply replaces the old one.

The Inputs tab on the same page shows the digital inputs live. On the KC868-AP, that is sixteen inputs through two PCF8574 expanders, plus two direct inputs. Wall buttons and sensors connect here, and automations can react to them.

The Controllers tab lists other Lumen controllers on the same ESP-NOW network. We will come back to it later in the series.
