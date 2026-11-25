"""Observe one causally tagged CS4231 output sample on a single host clock."""
import base64
import json
import os
from pathlib import Path
import struct
import sys
import time
from urllib.request import urlopen

base = os.environ.get("NP21W_AIDEBUG_URL", "http://127.0.0.1:8025")
out = Path(sys.argv[1])
out.mkdir(exist_ok=False)

def window():
    with urlopen(base + "/api/sound?pcm=1", timeout=3) as response:
        body = response.read()
        arrived = time.monotonic_ns()
    reply = json.loads(body)
    raw = base64.b64decode(reply["pcm"], validate=True)
    assert reply["ok"] and reply["rate"] == 44100 and len(raw) == reply["frames"] * 4
    return reply["frames_total"], raw, arrived, body

end, raw, _, body = window()
(out / "before.json").write_bytes(body)
prior = list(struct.iter_unpack("<hh", raw))
assert any(10000 <= r < 26000 for _, r in prior) and all(r != -23456 for _, r in prior)
sent = time.monotonic_ns()
with urlopen(base + "/api/key", data=b"text=x", timeout=3) as response:
    assert json.load(response)["ok"]
while time.monotonic_ns() - sent < 2_000_000_000:
    stop, raw, arrived, body = window()
    start = stop - len(raw) // 4
    assert start <= end <= stop, "gap or reset"
    data = list(struct.iter_unpack("<hh", raw))
    hits = [i for i in range(end-start, len(data)) if data[i][1] == -23456]
    end = stop
    if not hits:
        continue
    assert len(hits) == 1
    i = hits[0]
    # The previous positive marker identifies the application frame, including
    # when the injected marker replaced a regular period marker.
    j = next(j for j in range(i-1, max(-1, i-257), -1) if 10000 <= data[j][1] < 26000)
    frame = (data[j][1] - 10000) * 256 + i - j
    result = dict(key_send_ns=sent, sound_received_ns=arrived, frame=frame,
                  capture_frame=start+i, upper_ns=arrived-sent)
    (out / "hit.json").write_bytes(body)
    (out / "timing.json").write_text(json.dumps(result) + "\n")
    print(result)
    sys.exit(0 if arrived-sent < 30_000_000 else 1)
sys.exit("no tagged output within 2 s")
