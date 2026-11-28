# ch4

The Overview is the first screen you see after signing in. It shows how many lights are on, every room, and your scenes.

A room is a group of lights in one space. Each room card has a power button and a single brightness slider that drives every light in it together.

When you move a room slider, each light keeps its relative level. The room shows an aggregate, and the count tells you how many of its lights are on.

The All off button switches every light on the controller off with one tap, and a scene chip brings them back.

Open a room to see its lights. Every light card shows controls for exactly what that light can do, and nothing more.

A dimmer gets a brightness slider. An RGB light adds a colour picker, with a hue strip and a saturation and brightness area, plus a hex field if you know the exact colour.

Lights that support colour also offer effects such as candlelight, sunrise, rainbow, breathing and colour cycle. Effects run inside the firmware, so they keep running when you close the browser.

A tunable white light gets a colour temperature slider, from warm to cool white, within the limits you set for that light.

You never see a control that a light cannot use. That rule is built into the web app, which reads each light's capabilities from the firmware.

The web app stays live over a WebSocket. If a light changes from Home Assistant, an automation, a wall button or another browser, every open screen updates at once.

Rooms are created and edited from the Rooms page. Lumen also supports groups, which collect lights across rooms, such as everything downstairs, so that an automation or an MQTT message can control them with one command.
