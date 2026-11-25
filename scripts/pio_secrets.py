# PlatformIO pre-build script: turns the gitignored secrets.ini into a
# generated header used ONLY to seed development Wi-Fi credentials into NVS
# on first boot. Without secrets.ini (CI, production) the header is removed
# and the device starts its WPA2 setup access point instead.
import configparser
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

root = env.subst("$PROJECT_DIR")  # noqa: F821
header = os.path.join(root, "include", "lumen", "generated", "dev_secrets.h")
ini = os.path.join(root, "secrets.ini")


def c_escape(value):
    return value.replace("\\", "\\\\").replace('"', '\\"')


ssid = password = ""
if os.path.exists(ini):
    cfg = configparser.ConfigParser()
    cfg.read(ini)
    if cfg.has_section("secrets"):
        ssid = cfg.get("secrets", "wifi_ssid", fallback="").strip()
        password = cfg.get("secrets", "wifi_password", fallback="").strip()

# Always emit the header (empty values when there are no secrets) so the
# build's dependency tracking sees it and recompiles when it changes.
content = (
    "// GENERATED from secrets.ini by scripts/pio_secrets.py - do not commit.\n"
    "#pragma once\n"
    '#define LUMEN_DEV_WIFI_SSID "%s"\n'
    '#define LUMEN_DEV_WIFI_PASSWORD "%s"\n' % (c_escape(ssid), c_escape(password))
)
os.makedirs(os.path.dirname(header), exist_ok=True)
old = open(header).read() if os.path.exists(header) else None
if old != content:
    with open(header, "w") as f:
        f.write(content)
