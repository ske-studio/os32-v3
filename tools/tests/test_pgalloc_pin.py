#!/usr/bin/env python3
"""M1 pin transactions with the actual allocator and no libc."""
CHECK = dict(name='pgalloc-pin', tier=1, needs=[], budget=30,
    covers=['mm/pgalloc.c', 'mm/pgalloc_owner.inc', 'mm/pgalloc*.inc',
            'mm/pgalloc.h', 'mm/physmem.c', 'mm/physmem.h',
            'tools/tests/pgalloc_pin_host.c', 'tools/tests/pgalloc_host_fixture.h',
            'tools/tests/test_pgalloc_pin.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'])
import pathlib
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = pathlib.Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('pin-zero', 'pgalloc.c', '        pin_map[p] = 0;', '        pin_map[p] = 1;'),
    ('pin-offset', 'pgalloc.c', '    pin_map = owner_map + PGALLOC_OWNER_BYTES(limit);',
     '    pin_map = owner_map;'),
    ('free', 'pgalloc_owner.inc', ' || pin_map[pfn + i]) goto bad;', ') goto bad;'),
    ('transfer', 'pgalloc_owner.inc', 'else if (!pinned && pin_map[pfn + i])', 'else if (0)'),
    ('reclaim', 'pgalloc_owner.inc', 'if (page_owned(p, owner) && pin_map[p])', 'if (0)'),
]

def run_case(mutation=None):
    with tempfile.TemporaryDirectory(prefix='pin-') as tmp:
        tmp = pathlib.Path(tmp)
        for path in [ROOT / 'mm/pgalloc.c', *(ROOT / 'mm').glob('pgalloc*.inc')]:
            data = path.read_text()
            if mutation and path.name == mutation[1]:
                _, _, old, new = mutation
                if data.count(old) != 1:
                    raise RuntimeError('mutation anchor is not unique')
                data = data.replace(old, new)
            (tmp / path.name).write_text(data)
        cmd = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-Wall', '-Wextra',
               '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
               '-nostdlib', '-static', '-no-pie', '-ffunction-sections', '-Wl,--gc-sections',
               '-Wl,--defsym=__kernel_resident_end=0x375000',
               '-Wl,--defsym=__ledger_fixed_start=0x17d000',
               '-Wl,--defsym=__ledger_fixed_end=0x180000', '-I' + str(tmp),
               '-I' + str(ROOT / 'tools/tests'), *include_flags(ROOT),
               str(ROOT / 'tools/tests/pgalloc_pin_host.c'), str(ROOT / 'mm/physmem.c'),
               '-o', str(tmp / 'test')]
        subprocess.run(cmd, check=True)
        return host32.run([str(tmp / 'test')], timeout=60).returncode

if __name__ == '__main__':
    if run_case() != 0:
        raise SystemExit('baseline failed')
    print('baseline GREEN', flush=True)
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS:
            rc = run_case(mutation)
            if rc == 0:
                raise SystemExit(mutation[0] + ' survived')
            print(mutation[0] + ' RED rc=' + str(rc), flush=True)


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=" ".join(ctx.runners))
    return subprocess.call([sys.executable, __file__, *(["--mutate"] if ctx.mutate else [])], cwd=ctx.root, env=env)
