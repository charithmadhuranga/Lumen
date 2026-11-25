"""Create the demo lights/rooms used by the on-device tests (idempotent)."""
import sys
from lumen_client import Lumen, ApiError

def setup(L):
    def ok(fn, *a):
        try: return fn(*a)
        except ApiError as e:
            if e.status != 409: raise
    ok(L.post, "/rooms", {"id": "living", "name": "Living Room", "floor": "Ground floor"})
    ok(L.post, "/rooms", {"id": "kitchen", "name": "Kitchen"})
    ok(L.post, "/lights", {"id": "ceiling", "name": "Ceiling", "type": "dimmer", "roomId": "living", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 0}})
    ok(L.post, "/lights", {"id": "tv_strip", "name": "TV strip", "type": "rgb", "roomId": "living", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 1}})
    ok(L.post, "/lights", {"id": "counter", "name": "Counter", "type": "cct", "roomId": "kitchen", "driver": {"type": "pca9685", "deviceId": "pca9685_0", "channel": 4}})
    ok(L.post, "/lights", {"id": "porch", "name": "Porch", "type": "switch", "driver": {"type": "relay", "deviceId": "relay1", "channel": 0}})

if __name__ == "__main__":
    L = Lumen(sys.argv[1]); L.login("admin", sys.argv[2]); setup(L); print("demo data ready")
