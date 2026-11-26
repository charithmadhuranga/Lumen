"""Install a firmware image on one or more controllers over the network and
wait until each is back and has confirmed the new image.

    python3 ota_install.py <password> <firmware.bin> http://host1 [http://host2 ...]
"""
import sys
import time

import requests

from lumen_client import Lumen

pw, image, hosts = sys.argv[1], sys.argv[2], sys.argv[3:]
for h in hosts:
    L = Lumen(h)
    L.login("admin", pw)
    r = L.s.post(h + "/api/v1/ota/upload", data=open(image, "rb"), timeout=300,
                 headers={"Content-Type": "application/octet-stream", "X-CSRF-Token": L.csrf})
    print(h, "upload", r.status_code, r.json().get("error") or "")
for h in hosts:
    time.sleep(8)
    for _ in range(90):
        try:
            if requests.get(h + "/api/v1/system/health", timeout=2).ok:
                break
        except requests.RequestException:
            pass
        time.sleep(1)
    for _ in range(30):
        L = Lumen(h)
        L.login("admin", pw)
        st = L.get("/ota/status")["running"]
        if st["state"] == "valid":
            break
        time.sleep(2)
    print(h, "running", st)
