# ch5

A scene is a snapshot of lights. Recalling it sends each light back to its saved state with a smooth fade.

The quickest way to make a scene is to set the lights the way you like them, then press Save current state on the Scenes page.

Give the scene a name and choose which lights it covers. Lights you leave out are simply not touched when the scene runs.

That matters. A movie scene might dim the living room, but it should not switch off the porch light outside.

Each scene card shows how many lights it covers and its fade time. Press Activate, and every light moves to its saved level at the same time.

Scenes can also be built by hand with the New button, edited later, duplicated, and imported or exported as files.

Scenes are available everywhere else in Lumen. Automations can activate them, Home Assistant sees them through MQTT, and on a network of controllers, one scene can be activated on every controller at once.
