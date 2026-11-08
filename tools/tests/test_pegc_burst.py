"""Run PEGC HW-BASIC with width-sensitive store loss and restoration checks."""
CHECK = dict(name='pegc-burst', tier=1, needs=[], budget=60, covers=[
    'userland/tests/hw/pegc_burst.hwb', 'tools/tests/pegc_burst_host.c',
    'tools/tests/test_pegc_burst.py', 'tools/tests/hwb_fake.h',
    'tools/tests/hwb_host.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'userland/shell/hwb.h', 'userland/shell/hwb_parse.c', 'userland/shell/hwb_exec.c',
    'userland/shell/hwb_probe.c', 'userland/shell/hwb_log.c',
    'kdebug/hwb_wire.h', 'kdebug/probe_wire.h', 'include/types.h',
    'sdk/include/os32/os32_kapi_shared.h'])
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import hwb_host

MUTATIONS = [
    ('if (sys16m & 4) != 0 goto unavailable', 'if (sys16m & 0) != 0 goto unavailable'),
    ('if variant != 3 goto variants', 'if variant != 2 goto variants'),
    ('mmioread16 phys (base + off) -> discard', 'log "read omitted"'),
    ('mmiowrite8 phys (base + (i << 2) + lane) (original[i] >> (lane << 3)) & 0xFF',
     'mmiowrite8 phys (base + (i << 2) + lane) 0'),
    ('print "mismatch-count" bad', 'print "mismatch-count" 0'),
]

def main():
    root = hwb_host.ROOT
    original = (root / 'userland/tests/hw/pegc_burst.hwb').read_text()
    fixture = (root / 'tools/tests/pegc_burst_host.c').read_text()
    with tempfile.TemporaryDirectory(prefix='pegc-burst-') as td:
        tmp = Path(td)
        for index, mutation in enumerate([None] + (MUTATIONS+[None] if '--mutate' in sys.argv else [])):
            script = original
            if mutation:
                old, new = mutation
                assert script.count(old) == 1
                script = script.replace(old, new)
            source = tmp / 'fixture.c'
            source.write_text(fixture.replace('SCRIPT_DATA', json.dumps(script)))
            exe = tmp / 'host'
            hwb_host.build(root, source, exe)
            result = hwb_host.execute(exe)
            assert result.returncode == (1 if mutation else 0), result
            print(('RED runtime ' if mutation else 'GREEN ') + str(index), flush=True)
            if index == 0:
                hwb_host.build(root, source, tmp / 'target.o', cross=True)
    return 0

def run(ctx):
    return subprocess.call([sys.executable, '-B', __file__,
        *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
        env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))

if __name__ == '__main__':
    raise SystemExit(main())
