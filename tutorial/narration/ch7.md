# ch7

Automations in Lumen run on the controller itself. They need no internet connection, no cloud, and no Home Assistant.

Every automation follows the same pattern. A trigger starts it, optional conditions decide whether it may continue, and a list of actions runs in order.

Triggers include a light changing, an input or a wall button being pressed, double pressed or held, a time of day, sunrise or sunset, an MQTT message, an event from another controller, a scene being activated, and the controller starting up.

For sunrise and sunset, the controller needs to know where it is. You enter the latitude and longitude once, and it calculates the sun times locally, every day.

Conditions can limit an automation to a time window or certain days of the week, or compare the state of a light, an input or a variable. They can be combined with and, or and not.

Actions can switch, dim or colour lights, set colour temperature, activate scenes, control rooms and groups, run effects, wait, publish MQTT messages, send UDP or TCP messages, set variables, or start another automation.

The simple editor is a form. You choose the trigger, add conditions, and list the actions. A sentence at the top reads the automation back in plain language, so you can check it at a glance.

The advanced tab shows the same automation as a node graph. You can wire triggers, logic gates and actions visually, and both views stay in sync.

Before an automation is saved, the firmware validates it. It checks that every light, scene and input it refers to exists, and it detects loops, where an automation would trigger itself again and again.

Each automation also has execution limits: a cooldown, a maximum number of runs per hour, and whether it may run in parallel with itself.

You can test any automation on the spot with the Test button, and the history shows when each automation ran and what happened.
