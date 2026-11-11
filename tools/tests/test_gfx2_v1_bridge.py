#!/usr/bin/env python3
"""C14: real bridge, surface table and C7 lock/revoke, without libc."""
from pathlib import Path
import argparse
import re
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('timeout-keeps-lock', 'if (rc == OS32_ERR_TIMEOUT) continue;', '(void)rc;'),
    ('revoke-before-update', '(void)gfx2_lock_revoke(s);', '(void)0;'),
    ('alias-update', '(void)gfx2_surf_alias_update(s, has ? &d : NULL);', '(void)has;'),
    ('lock-busy', 's->state == GFX2_ST_LOCKED_RW ||', '0 ||'),
    ('lease-busy', '(sf && sf->lease_count)', '(sf && 0)'),
    ('wait', 'else rc = gfx2_cpu_begin(s, access, GFX2_BRIDGE_DL);', 'else rc = 0;'),
    ('flush', 'gfx2_cpu_end(s, access, &all);', '(void)all; (void)access;'),
    ('shadow-backing', 'mode == GFXSYNC_SHADOW)', 'mode == 99)'),
]
def observe(work, source):
    target = work / 'bridge.c'
    target.write_text(source)
    # Compile the actual v1 fill/blit entries, including both SHADOW aliases.
    api = (ROOT / 'gfx/gfx_core_api.inc').read_text()
    entries = ['#include "gfx2_int.h"']
    for name in ['gfx_hw_fill_rect', 'gfx_hw_blit']:
        match = re.search(r'int __cdecl ' + name + r'\([^\n]*\)\n\{', api)
        assert match, name
        end, depth = match.end(), 1
        while depth:
            depth += (api[end] == '{') - (api[end] == '}')
            end += 1
        entries.append(api[match.start():end])
    calls = work / 'calls.c'
    calls.write_text('\n'.join(entries))
    exe = work / 'bridge-test'
    host32.build(['gcc', '-m32', '-std=gnu11', '-O0', '-Wall', '-Wextra', '-Werror',
        '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static',
        '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
        '-I' + str(ROOT / 'gfx/v2'), *include_flags(ROOT), str(target),
        str(calls), str(ROOT / 'gfx/v2/lock.c'), str(ROOT / 'gfx/v2/surface.c'),
        str(ROOT / 'tools/tests/gfx2_v1_bridge_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/v1_bridge.c').read_text()
    with tempfile.TemporaryDirectory(prefix='gfx2-bridge-') as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, f'normal runtime rc={rc}'
        print('GREEN C14 aliases, waits, lock/rebind, STALE and staging reclamation')
        if args.mutate:
            for name, anchor, replacement in MUTATIONS:
                assert source.count(anchor) == 1, name
                rc = observe(work, source.replace(anchor, replacement))
                assert rc > 0, f'{name}: expected runtime RED, rc={rc}'
                print(f'RED {name}: runtime rc={rc}')
if __name__ == '__main__':
    main()
