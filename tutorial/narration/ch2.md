# ch2

Every Lumen release on GitHub includes ready-to-flash files for each chip.

For a new board, you only need one of them: the full image. It contains the bootloader, the partition table, the firmware and the web app.

You can write it with esptool from the command line, at address zero. If you prefer not to install anything, the browser-based esptool from Espressif works in Chrome or Edge.

The flash is laid out as two firmware slots of one and a half megabytes each, plus a LittleFS partition for the web app and your configuration. Two slots are what make safe updates possible later.

On first start, the controller has no Wi-Fi to join, so it opens its own setup hotspot. The hotspot is always WPA2 protected, and its password is printed on the serial console at boot.

Join the hotspot, and open the address one nine two dot one six eight dot four dot one in your browser.

You are greeted by the sign-in page. The first sign-in uses the default account, admin with the password admin.

Lumen does not let you stop there. The default password must be replaced before any other screen opens.

As you type the new password, a strength meter checks it. The password needs at least ten characters.

The firmware never stores the password itself. It stores a salted PBKDF2 hash with SHA-256, and only in NVS, never in a configuration file or a backup.

Once the password is saved, you land on an empty overview. The controller is ready, but it has no lights yet.

Open the Network page, enter your Wi-Fi name and password, and save. The controller joins your network and becomes reachable by name, at lumen dash, followed by the last four characters of its address, dot local.

From now on, the setup hotspot only comes back if Wi-Fi is unreachable, and it is never an open hotspot.

In the next chapter we add lights.
