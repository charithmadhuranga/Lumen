# ch9

Lumen works on its own, but it also fits into a larger smart home. The Integrations page connects it to MQTT, Home Assistant and your own software.

To use MQTT, switch it on and enter your broker's address, port, username and password. TLS is supported too.

The password field is write-only. Once saved, it is kept in NVS and never sent back to the browser, and it is never included in a backup.

The status line shows whether the broker is connected, along with counters for messages sent, received and rejected.

Each light gets a command topic and a state topic under a base topic for the controller. Groups and scenes have their own topics, and a status topic tells subscribers when the controller goes offline.

With Home Assistant discovery switched on, you do not configure anything in Home Assistant. The controller appears there as one device, with every light, and with diagnostic sensors such as free memory and Wi-Fi signal.

Brightness, colour, colour temperature and effects appear in Home Assistant exactly as each light supports them.

Control works in both directions. Switch a light in Home Assistant, and the command travels over MQTT to the real output. Switch it in Lumen, at a wall button, or from an automation, and Home Assistant updates within a moment.

When you add, rename or delete a light in Lumen, Home Assistant follows automatically.

If the broker or Wi-Fi goes down, lighting and local automations keep running. Lumen reconnects on its own and publishes the current state again.

For your own apps, there is also a local TCP server that requires a sign-in with an integration token, and a UDP protocol where every command is signed with HMAC SHA-256 and protected against replay.
