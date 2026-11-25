"""Read the PIT-calibrated rate from the matching tick-probe kernel."""
import json
import os
from pathlib import Path
import re
import struct
import subprocess
from urllib.request import urlopen

image = Path("build/out/tick-probe")
unit = image / "obj/platform/pc98/tsc_clock.o"
text = (image / "kernel.map").read_text()
match = re.search(r"^ \.bss\s+(0x[0-9a-f]+)\s+0x[0-9a-f]+\s+" + re.escape(str(unit)) + r"$", text, re.M)
base = int(match[1], 16)
syms = {name: base + int(addr, 16) for addr, name in re.findall(
    r"^([0-9a-f]+) b (\w+)$", subprocess.check_output(["nm", str(unit)], text=True), re.M)}
url = os.environ.get("NP21W_AIDEBUG_URL", "http://127.0.0.1:8025")
def words(addr, count):
    with urlopen(f"{url}/api/mem?space=phys&addr={addr:#x}&len={4*count}", timeout=3) as response:
        data = json.load(response)
    return struct.unpack("<" + "I"*count, bytes.fromhex(data["hex"]))
def rate():
    state, = words(syms["state"], 1)
    generation, = words(syms["tsc_generation"], 1)
    mult, shift = words(syms["epoch"] + 16, 2)
    assert state == 2 and mult > 0 and 1 <= shift <= 32, "TSC not calibrated"
    return dict(mult=mult, shift=shift, generation=generation)
first = rate()
assert first == rate(), "calibration changed"
print(json.dumps(first))
