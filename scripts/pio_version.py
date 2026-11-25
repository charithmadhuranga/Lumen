# PlatformIO pre-build script: firmware version from version.txt (single
# source; CMakeLists.txt reads the same file into PROJECT_VER, which ends up
# in the app descriptor checked by OTA). LUMEN_VERSION overrides it for test
# images, e.g. LUMEN_VERSION=0.2.0-ota-test pio run -e esp32.
import os

Import("env")  # noqa: F821  (provided by PlatformIO)

root = env.subst("$PROJECT_DIR")  # noqa: F821
version = os.environ.get("LUMEN_VERSION", "").strip()
if not version:
    with open(os.path.join(root, "version.txt"), encoding="utf-8") as f:
        version = f.read().strip()
if not version or len(version) > 31 or any(c in version for c in '"\\ '):
    raise SystemExit(f"invalid firmware version {version!r}")
env.Append(CPPDEFINES=[("LUMEN_FIRMWARE_VERSION", env.StringifyMacro(version))])  # noqa: F821

# CMake caches PROJECT_VER (app descriptor): force a reconfigure whenever the
# effective version changes, e.g. between an LUMEN_VERSION test image and a
# normal build.
build_dir = env.subst("$BUILD_DIR")  # noqa: F821
stamp = os.path.join(build_dir, "lumen_version.txt")
previous = open(stamp, encoding="utf-8").read().strip() if os.path.exists(stamp) else None
if previous != version and env.subst("$PIOPLATFORM") != "native":  # noqa: F821
    cache = os.path.join(build_dir, "CMakeCache.txt")
    if os.path.exists(cache):
        os.remove(cache)
    os.makedirs(build_dir, exist_ok=True)
    with open(stamp, "w", encoding="utf-8") as f:
        f.write(version + "\n")

# OTA rollback drills only (never for release images):
#   LUMEN_OTA_SELFTEST=1  image never passes its health check -> rolls back
#   LUMEN_OTA_SELFTEST=2  image crashes shortly after boot    -> rolls back
selftest = os.environ.get("LUMEN_OTA_SELFTEST", "").strip()
if selftest in ("1", "2"):
    env.Append(CPPDEFINES=[("LUMEN_OTA_SELFTEST", int(selftest))])  # noqa: F821
    print(f"*** OTA self-test image (LUMEN_OTA_SELFTEST={selftest}) — not for release ***")
