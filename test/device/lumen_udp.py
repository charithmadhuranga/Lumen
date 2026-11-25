"""Lumen UDP control client (protocol v1, see docs/API.md "UDP control").

Frame: {"v":1,"ts":<unix s>,"seq":<u32>,"src":"<id>","cmd":{...},"mac":"<hex>"}
mac = HMAC-SHA256(integration token, v|ts|seq|src|compact(cmd)) where
compact(cmd) is the command JSON without whitespace, keys in sent order,
integers only (the controller re-serialises it with cJSON).

    python3 lumen_udp.py <controller-ip> <token> '{"type":"light","light_id":"ceiling","state":"ON"}'
"""
import hashlib
import hmac
import json
import socket
import sys
import time


def frame(token: str, cmd: dict, seq: int, src: str = "pc", ts: int | None = None) -> bytes:
    ts = int(time.time()) if ts is None else ts
    compact = json.dumps(cmd, separators=(",", ":"))
    canon = f"1|{ts}|{seq}|{src}|{compact}"
    mac = hmac.new(token.encode(), canon.encode(), hashlib.sha256).hexdigest()
    return json.dumps({"v": 1, "ts": ts, "seq": seq, "src": src, "cmd": cmd, "mac": mac}).encode()


def send(host: str, data: bytes, port: int = 8267, timeout: float = 2.0):
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(timeout)
    s.sendto(data, (host, port))
    try:
        return json.loads(s.recv(1024))
    except socket.timeout:
        return None
    finally:
        s.close()


if __name__ == "__main__":
    host, token, cmd = sys.argv[1], sys.argv[2], json.loads(sys.argv[3])
    print(send(host, frame(token, cmd, seq=int(time.time() * 1000) & 0xFFFFFFFF)))
