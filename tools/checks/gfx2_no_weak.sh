#!/bin/sh
# Final-image gate; optional paths let the host fixture exercise real ELF/maps.
set -eu
exec python3 - "$@" <<'PY'
import os
from pathlib import Path
import re
import subprocess
import sys
if len(sys.argv) not in (2, 7) or sys.argv[1] not in ('I1', 'I2', 'I3'):
    sys.exit('usage: gfx2_no_weak.sh I1|I2|I3 [manifest kernel.elf kernel.map geforce.elf geforce.map]')
root = Path.cwd()
stage = sys.argv[1]
paths = sys.argv[2:] or ['tools/checks/gfx2_wiring.txt', 'build/out/kernel.elf',
    'build/out/kernel.map', 'build/out/geforce/geforce.elf', 'build/out/geforce/geforce.map']
manifest, ke, km, ge, gm = map(Path, paths)
rows = []
for line in manifest.read_text().splitlines():
    line = line.split('#', 1)[0].strip()
    if not line:
        continue
    fields = line.split()
    if len(fields) != 3 or fields[0] not in ('I1', 'I2', 'I3') or fields[1] not in ('kernel', 'geforce', 'kernel-data'):
        sys.exit('invalid wiring row: ' + line)
    if fields[0] == stage:
        rows.append(fields[1:])
if not rows or len({tuple(r) for r in rows}) != len(rows):
    sys.exit('empty or duplicate wiring stage')
failed = False
for target, elf, mapfile in [('kernel', ke, km), ('geforce', ge, gm)]:
    names = [(t, n) for t, n in rows if t.split('-')[0] == target]
    if not names:
        continue
    symbols = {}
    nm = os.environ.get('NM', 'nm')
    for line in subprocess.check_output([nm, '-n', str(elf)], text=True).splitlines():
        match = re.fullmatch(r'([0-9a-fA-F]+)\s+(\w)\s+(\S+)', line.strip())
        if match:
            address, kind, name = match.groups()
            symbols[name] = (int(address, 16), kind)
    text = mapfile.read_text()
    # GNU ld splits long input section names over two lines. Whitespace spans
    # both forms; input code/data sections with an object path are intervals.
    ranges = []
    for match in re.finditer(r'^\s+\.(?:text|rodata|data|bss)(?:\.\S+)?\s+(0x[0-9a-fA-F]+)\s+(0x[0-9a-fA-F]+)\s+(\S+\.o)\s*$', text, re.M):
        start, size, obj = match.groups()
        if re.search(r'(?:a0_weak|gf_v2_weak)\.o$', obj):
            ranges.append((int(start, 16), int(start, 16) + int(size, 16)))
    for category, name in names:
        allowed = ('R', 'D', 'B', 'r', 'd', 'b') if category.endswith('-data') else ('T', 't')
        symbol = symbols.get(name)
        if (symbol is None or symbol[1] not in allowed or
                any(lo <= symbol[0] < hi for lo, hi in ranges)):
            print(f'FAIL {stage} {target} {name}: {symbol}')
            failed = True
if failed:
    sys.exit(1)
print('PASS ' + stage + ' strong symbols outside A0 sections')
PY
