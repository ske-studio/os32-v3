"""Observe one causally tagged CS4231 output sample on a single host clock."""
import base64
import json
import os
from pathlib import Path
import struct
import sys
import time
from urllib.request import urlopen

def window(base):
    with urlopen(base + "/api/sound?pcm=1", timeout=3) as response:
        body = response.read()
        arrived = time.monotonic_ns()
    reply = json.loads(body)
    raw = base64.b64decode(reply["pcm"], validate=True)
    assert reply["ok"] and reply["rate"] == 44100 and len(raw) == reply["frames"] * 4
    return reply["frames_total"], raw, arrived, body


def locate(prior, fresh):
    hits = [i for i, (_, right) in enumerate(fresh) if right == -23456]
    if not hits:
        return None
    assert len(hits) == 1
    offset = hits[0]
    data = prior[-256:] + fresh[:offset]
    for distance, (_, right) in enumerate(reversed(data[-256:]), 1):
        if 10000 <= right < 26000:
            return offset, (right - 10000) * 256 + distance
    raise ValueError("no period marker within 256 frames before tag")


if __name__ == "__main__":
    base = os.environ.get("NP21W_AIDEBUG_URL", "http://127.0.0.1:8025")
    out = Path(sys.argv[1])
    out.mkdir(exist_ok=False)

    end, raw, _, body = window(base)
    (out / "before.json").write_bytes(body)
    prior = list(struct.iter_unpack("<hh", raw))
    assert any(10000 <= r < 26000 for _, r in prior) and all(r != -23456 for _, r in prior)
    sent = time.monotonic_ns()
    with urlopen(base + "/api/key", data=b"text=x", timeout=3) as response:
        assert json.load(response)["ok"]
    while time.monotonic_ns() - sent < 2_000_000_000:
        stop, raw, arrived, body = window(base)
        start = stop - len(raw) // 4
        assert start <= end <= stop, "gap or reset"
        data = list(struct.iter_unpack("<hh", raw))
        fresh = data[end-start:]
        capture_start = end
        end = stop
        hit = locate(prior, fresh)
        prior = (prior + fresh)[-256:]
        if hit is None:
            continue
        offset, frame = hit
        result = dict(key_send_ns=sent, sound_received_ns=arrived, frame=frame,
                      capture_frame=capture_start+offset, upper_ns=arrived-sent)
        (out / "hit.json").write_bytes(body)
        (out / "timing.json").write_text(json.dumps(result) + "\n")
        print(result)
        sys.exit(0)
    sys.exit("no tagged output within 2 s")
