# Installing and updating Lumen firmware

This guide covers the files attached to each Lumen release and how to put
them on a controller. Pick the section that matches what you want to do:

- **Fresh install** (new board, or start over from scratch): [section 2](#2-fresh-install).
- **Update** a controller that already runs Lumen and keep its settings: [section 3](#3-update-an-existing-controller).

## 1. Release files

Replace `X.Y.Z` with the release version and `<chip>` with `esp32` (KinCony
KC868-AP and other ESP32 boards) or `esp32s3` (ESP32-S3 boards).

| File | What it is | When to use it |
|---|---|---|
| `lumen-X.Y.Z-<chip>-full.bin` | Complete 4 MB flash image | Fresh install. Written at address `0x0`. **Erases everything on the board.** |
| `lumen-X.Y.Z-<chip>-app.bin` | Firmware only | Updating a running controller (Settings → Firmware). Keeps all settings. |
| `lumen-X.Y.Z-web.pack` | Web interface | Updating the web interface (Settings → Web interface, or `/recovery`). Same file for every chip. |
| `lumen-X.Y.Z-<chip>-bootloader.bin`, `-partitions.bin`, `-ota_data_initial.bin`, `-littlefs.bin` | The individual parts of the full image | Only if your flashing tool cannot write one merged file. |
| `lumen-X.Y.Z-<chip>-flash_args.txt` | Addresses of the individual parts | Used with `esptool.py ... write_flash @file`. |
| `lumen-X.Y.Z-<chip>-manifest.json` | ESP Web Tools manifest | For hosting a browser installer. |
| `lumen-X.Y.Z-<chip>.zip` | All of the above for one chip, plus this guide | Convenience download. |
| `SHA256SUMS.txt` | Checksums | To check a download (section 5). |

Flash layout (4 MB, identical for both chips):

| Address | Size | Contents |
|---|---|---|
| `0x1000` (ESP32) / `0x0` (ESP32-S3) | | bootloader |
| `0x8000` | 3 KB | partition table |
| `0x9000` | 24 KB | NVS: passwords, Wi-Fi credentials, keys |
| `0xF000` | 8 KB | OTA selection |
| `0x20000` | 1536 KB | firmware slot A |
| `0x1A0000` | 1536 KB | firmware slot B |
| `0x320000` | 896 KB | LittleFS: configuration, web interface, floor plans |

ESP32-S3 images are built and checked by CI but have **not yet been tested on
ESP32-S3 hardware**. The ESP32 images are tested on the KinCony KC868-AP.

## 2. Fresh install

A fresh install writes the whole flash. Any previous configuration, Wi-Fi
credentials and passwords on the board are erased.

### 2.1 What you need

- A USB data cable (not a charge-only cable) from your computer to the
  board's USB port.
- The USB-serial driver for the board's USB chip if your system does not
  have it. The KC868-AP uses a WCH CH340-family chip; most ESP32 development
  boards use a CH340 or a Silicon Labs CP210x.
- The `lumen-X.Y.Z-<chip>-full.bin` file.

Find the serial port after plugging the board in:

| System | Port looks like |
|---|---|
| Linux | `/dev/ttyUSB0` or `/dev/ttyACM0` (add yourself to the `dialout` group if access is denied) |
| macOS | `/dev/cu.usbserial-XXXX` or `/dev/cu.wchusbserialXXXX` |
| Windows | `COM3`, `COM4`, … (Device Manager → Ports) |

**Safety:** disconnect mains-powered loads from the relay outputs while you
flash and test. Outputs are switched off during start-up, but the hardware
polarity of the KC868-AP relays has not been verified.

### 2.2 Option A — browser (Chrome or Edge)

1. Open <https://espressif.github.io/esptool-js/> in Chrome or Edge (the
   browser needs Web Serial; Firefox and Safari do not have it).
2. Set the baud rate to `460800` and click **Connect**. Choose the board's
   serial port.
3. Under **Flash Address**, enter `0x0` and choose
   `lumen-X.Y.Z-<chip>-full.bin`.
4. Click **Program** and wait until it reports that it is finished.
5. Press the board's reset button (or unplug and replug it).

### 2.3 Option B — command line (esptool)

Install esptool once (Python 3 required):

```sh
pip install esptool
```

Write the full image (replace the port and file names):

```sh
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash 0x0 lumen-X.Y.Z-esp32-full.bin
```

For an ESP32-S3 board use `--chip esp32s3` and the `esp32s3` file. Newer
esptool versions call the command `esptool` and accept `write-flash`; both
spellings work.

If you prefer the individual files, they give the same result:

```sh
esptool.py --chip esp32 --port /dev/ttyUSB0 --baud 460800 write_flash @lumen-X.Y.Z-esp32-flash_args.txt
```

Run this from the folder that contains the files.

If esptool reports `Failed to connect`: hold the board's **BOOT** (IO0)
button, press and release **RESET** (EN), start the command, then release
**BOOT**. Lower `--baud` to `115200` if the transfer fails part-way.

### 2.4 First start

1. The controller starts its setup hotspot **`Lumen-XXXX`** (the last four
   characters of its MAC address). The hotspot uses WPA2 with a password that
   is unique to the board.
2. To read that password, open a serial monitor at 115200 baud while the
   board starts, for example:

   ```sh
   python -m serial.tools.miniterm /dev/ttyUSB0 115200
   ```

   or `pio device monitor -p /dev/ttyUSB0 -b 115200`. Look for the line
   `setup AP 'Lumen-XXXX' (WPA2) password: …`. Press the reset button if
   you missed it.
3. Connect your phone or computer to the `Lumen-XXXX` network and open
   <http://192.168.4.1>.
4. Sign in with user **`admin`** and password **`admin`**. You must choose a
   new password straight away; the default cannot be kept.
5. Open **Network**, enter your Wi-Fi network and save. The controller joins
   it and turns the hotspot off once the connection is stable. It comes back
   on by itself if the Wi-Fi network is unreachable.
6. From then on, open <http://lumen-xxxx.local> (lower case), or the address
   your router gave the controller.
7. Open **Hardware** and choose your board profile (the KinCony KC868-AP
   profile is built in), then add your lights.

## 3. Update an existing controller

Updating over the network keeps all settings, lights, scenes, automations,
floor plans and passwords.

1. Optional but recommended: **Settings → Backup → Download backup**.
2. **Settings → Firmware**: choose `lumen-X.Y.Z-<chip>-app.bin` and install.
   The controller checks the file (chip, size, image integrity), installs it
   into the inactive firmware slot and restarts.
3. **Settings → Web interface**: choose `lumen-X.Y.Z-web.pack` and install.
   Install the web interface from the same release as the firmware.
4. Sign in again. **Settings** shows the running version.

Automatic rollback: a new firmware becomes permanent only after it has run
healthily for about 20 seconds (web server up and lighting loop running). If
it crashes or does not become healthy, the controller returns to the
previous firmware by itself.

If the web interface does not load after an update, open
`http://<controller>/recovery`. That page is built into the firmware; sign
in there and install `lumen-X.Y.Z-web.pack` again.

The update files can also be sent from a script:

```sh
# after signing in (cookie + X-CSRF-Token), see docs/API.md
curl -b cookies.txt -H "X-CSRF-Token: $CSRF" -H "Content-Type: application/octet-stream" \
     --data-binary @lumen-X.Y.Z-esp32-app.bin http://lumen-xxxx.local/api/v1/ota/upload
```

Do **not** use these files to update a running controller:

- `-full.bin` erases all settings and passwords.
- `-littlefs.bin` replaces the whole file system, including the
  configuration.

The partition table has not changed since 2026-11-26. If a future release
changes it, its release notes will say that a full serial install is needed.

## 4. Two or more controllers (ESP-NOW)

Each controller is installed and updated separately. Controllers that share
the ESP-NOW network ID and key keep working together while one of them
updates. It drops offline for about 20 seconds and rejoins on its own.

## 5. Checking a download

```sh
sha256sum -c SHA256SUMS.txt --ignore-missing     # Linux
shasum -a 256 -c SHA256SUMS.txt --ignore-missing # macOS
```

On Windows: `certutil -hashfile lumen-X.Y.Z-esp32-full.bin SHA256`, then
compare with the line in `SHA256SUMS.txt`.

Release images are not signed, and secure boot and flash encryption are not
enabled. Download releases only from the project's GitHub releases page.

## 6. Troubleshooting

| Symptom | What to do |
|---|---|
| `Failed to connect to ESP32` | Use the BOOT + RESET sequence (2.3). Try another cable or USB port, and check the driver. |
| `Permission denied: /dev/ttyUSB0` (Linux) | `sudo usermod -aG dialout $USER`, then log out and back in. |
| Board restarts again and again after a fresh install | Erase the flash and install again: `esptool.py --chip esp32 --port … erase_flash`, then repeat 2.3. |
| No `Lumen-XXXX` hotspot | Wait 30 seconds after power-up. Check the serial monitor for errors. |
| `lumen-xxxx.local` does not open | Some Android phones do not resolve `.local` names. Use the IP address shown by your router or in **Network**. |
| Web page is blank after an update | Open `/recovery` and install the matching `web.pack` (section 3). |
| A firmware update did not stay | The new firmware failed its health check and was rolled back to the previous one. **Settings → Firmware** shows the running version; **Hardware → Diagnostics** shows the reset reason and recent errors. Include the JSON from `/api/v1/system/diagnostics` when you report it. |
