#!/usr/bin/env python3
"""Package a built PlatformIO environment as release files.

    python3 scripts/package_release.py <env> <version> <out_dir>

Run after `npm run build:fs` (webui), `pio run -e <env>` and
`pio run -e <env> -t buildfs`. Writes into <out_dir>:

    lumen-<version>-<env>-full.bin          whole flash image, write at 0x0
    lumen-<version>-<env>-app.bin           application only (OTA update file)
    lumen-<version>-<env>-bootloader.bin    \\
    lumen-<version>-<env>-partitions.bin     |  individual images
    lumen-<version>-<env>-ota_data_initial.bin  (offsets in flash_args.txt)
    lumen-<version>-<env>-littlefs.bin      /
    lumen-<version>-<env>-flash_args.txt    esptool offsets for the files above
    lumen-<version>-<env>-manifest.json     ESP Web Tools manifest (full image)
    lumen-<version>-web.pack                web UI update file

Refuses to package a test image (fault-injection routes or OTA self-test
code), an image whose embedded version differs from <version>, or an
application larger than its OTA slot.
"""
import csv
import json
import os
import shutil
import struct
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CHIPS = {"esp32": "esp32", "esp32s3": "esp32s3"}
BOOTLOADER_OFFSET = {"esp32": 0x1000, "esp32s3": 0x0}
FLASH = {"mode": "dio", "freq": "40m", "size": "4MB"}  # what `pio run -t upload` writes
TEST_MARKERS = [b"/test/heap/reserve", b"TEST IMAGE: fault-injection", b"OTA SELF-TEST IMAGE"]


def die(msg):
    print(f"package_release: {msg}", file=sys.stderr)
    sys.exit(1)


def partitions():
    rows = {}
    with open(os.path.join(ROOT, "partitions.csv"), encoding="utf-8") as f:
        for row in csv.reader(line for line in f if line.strip() and not line.lstrip().startswith("#")):
            name, _type, _sub, offset, size = (c.strip() for c in row[:5])
            rows[name] = (int(offset, 0), int(size, 0))
    return rows


def app_version(path):
    # esp_app_desc_t follows the 24-byte image header and 8-byte segment header.
    with open(path, "rb") as f:
        head = f.read(32 + 48)
    magic, = struct.unpack_from("<I", head, 32)
    if magic != 0xABCD5432:
        die(f"{path}: no ESP-IDF app descriptor")
    return head[32 + 16:32 + 48].split(b"\0", 1)[0].decode()


def esptool():
    try:
        import esptool  # noqa: F401
        return [sys.executable, "-m", "esptool"]
    except ImportError:
        bundled = os.path.expanduser("~/.platformio/packages/tool-esptoolpy/esptool.py")
        if os.path.exists(bundled):
            return [sys.executable, bundled]
    die("esptool not found (pip install esptool, or install the PlatformIO espressif32 platform)")


def main():
    if len(sys.argv) != 4:
        die(__doc__.strip().splitlines()[2].strip())
    env, version, out = sys.argv[1:]
    if env not in CHIPS:
        die(f"unknown env {env!r}")
    build = os.path.join(ROOT, ".pio", "build", env)
    parts = partitions()
    app = os.path.join(build, "firmware.bin")
    fs = os.path.join(build, "littlefs.bin")
    pack = os.path.join(ROOT, "data", "web", "web.pack")
    for p in (app, fs, pack):
        if not os.path.exists(p):
            die(f"missing {os.path.relpath(p, ROOT)} — build the web UI, firmware and filesystem image first")

    blob = open(app, "rb").read()
    for marker in TEST_MARKERS:
        if marker in blob:
            die(f"{app} is a test image (contains {marker.decode()!r}); rebuild without LUMEN_TEST_HOOKS / LUMEN_OTA_SELFTEST")
    embedded = app_version(app)
    if embedded != version:
        die(f"firmware reports version {embedded!r}, expected {version!r} (version.txt / LUMEN_VERSION)")
    slot = parts["ota_0"][1]
    if len(blob) > slot:
        die(f"application is {len(blob)} bytes, larger than its {slot}-byte OTA slot")
    if os.path.getsize(fs) != parts["storage"][1]:
        die(f"littlefs.bin is {os.path.getsize(fs)} bytes, partition is {parts['storage'][1]}")

    os.makedirs(out, exist_ok=True)
    name = lambda part: os.path.join(out, f"lumen-{version}-{env}-{part}")  # noqa: E731
    # Offsets as `pio run -t upload` writes them: bootloader per chip, partition
    # table at 0x8000, the rest from partitions.csv. (No `pio` call here: it
    # can reconfigure the build directory.)
    images = [  # (offset, source, published name)
        (BOOTLOADER_OFFSET[env], os.path.join(build, "bootloader.bin"), name("bootloader.bin")),
        (0x8000, os.path.join(build, "partitions.bin"), name("partitions.bin")),
        (parts["otadata"][0], os.path.join(build, "ota_data_initial.bin"), name("ota_data_initial.bin")),
        (parts["ota_0"][0], app, name("app.bin")),
    ]
    for _off, src, _dst in images:
        if not os.path.exists(src):
            die(f"missing {os.path.relpath(src, ROOT)}")
    images.append((parts["storage"][0], fs, name("littlefs.bin")))
    images.sort()
    for _off, src, dst in images:
        shutil.copyfile(src, dst)
    shutil.copyfile(pack, os.path.join(out, f"lumen-{version}-web.pack"))

    chip = CHIPS[env]
    full = name("full.bin")
    cmd = esptool() + ["--chip", chip, "merge_bin", "-o", full, "--flash_mode", FLASH["mode"], "--flash_freq", FLASH["freq"], "--flash_size", FLASH["size"]]
    for off, _src, dst in images:
        cmd += [hex(off), dst]
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL)

    with open(name("flash_args.txt"), "w", encoding="utf-8") as f:
        # esptool argument file (no comments allowed): use as
        #   esptool.py --chip <chip> write_flash @lumen-...-flash_args.txt
        f.write(f"--flash_mode {FLASH['mode']} --flash_freq {FLASH['freq']} --flash_size {FLASH['size']}\n")
        for off, _src, dst in images:
            f.write(f"{hex(off)} {os.path.basename(dst)}\n")
    manifest = {
        "name": "Lumen",
        "version": version,
        "new_install_prompt_erase": True,
        "builds": [{"chipFamily": {"esp32": "ESP32", "esp32s3": "ESP32-S3"}[chip], "parts": [{"path": os.path.basename(full), "offset": 0}]}],
    }
    with open(name("manifest.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2)
        f.write("\n")
    print(f"packaged {env} {version}: app {len(blob)} / {slot} bytes ({100 * len(blob) / slot:.1f} % of the OTA slot)")
    for off, _src, dst in images:
        print(f"  {hex(off):>9}  {os.path.basename(dst)}")
    print(f"  {'0x0':>9}  {os.path.basename(full)} (merged)")


if __name__ == "__main__":
    main()
