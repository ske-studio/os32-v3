#!/usr/bin/env python3
"""I3: real display/surface lifetime and shipped sync configuration binding."""
import argparse
import json
from pathlib import Path
import subprocess
import tempfile
from test_gfx2_i1 import observe

ROOT = Path(__file__).resolve().parents[2]
CHECK = dict(name='gfx2-i3-host', tier=2, needs=['make:all'], budget=120,
    covers=['tools/tests/test_gfx2_i1.py', 'tools/tests/gfx2_i1_host.c', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py', 'tools/tests/paging_source.py', 'tools/tests/pgalloc_host_fixture.h', 'tools/tests/host_arch/**', 'gfx/v2/**', 'gfx/sync/**', 'gfx/gfx.h', 'gfx/gfx_core.c', 'gfx/gfx_policy.h', 'gfx/palette.c', 'gfx/palette.h', 'mm/**', 'include/**', 'sdk/include/**', 'lib/kstring.h', 'lib/kstring_c.c', 'lib/dma_contract.c', 'kernel/module/**', 'kernel/console/display_restore.h', 'exec/lease.c', 'exec/lease.h', 'exec/appslot.h', 'init/kernel.c', 'build/os32.ld', 'userland/tests/gfx2_i1.c', 'userland/deploy.yaml', 'tools/tests/test_gfx2_i3.py',
            'tools/tests/gfx2_i3_host.c', 'assets/gfx/sync.cfg', 'build/core.yaml',
            'userland/tests/gfx2_i3.c', 'build/app.conf', 'build/programs.mk',
            'fs/vfs.h', 'kernel/time/cpu_calibrate.h'])
EXTRA = ['gfx/v2/' + n + '.c' for n in ['enum', 'modeset', 'display', 'v1_bridge', 'format', 'check_op', 'submit', 'sw_fill']]
EXTRA += ['gfx/sync/' + n + '.c' for n in ['sync_cfg', 'sync_gap', 'sync_bind', 'sync_write']]
MUTATIONS = [
    ('shipped-gap', 'assets/gfx/sync.cfg', 'gap_ns=80', 'gap_ns=8'),
    ('scanout-retained', 'gfx/v2/surface.c',
     'if (s->scan_ref || !surf_done(s)) {', 'if (!surf_done(s)) {'),
    ('flip-completion', 'gfx/v2/display.c',
     'gfx2_scan_ref_swap(a, h, s);', '(void)s;'),
    ('closing-sweep', 'gfx/v2/display.c', 'gfx2_closing_sweep(a);', '(void)a;'),
    ('restore-on-leave', 'exec/lease.c',
     '(void)gfx2_head_restore_v1(a, h);', '(void)h;'),
    ('cfg-bind', 'gfx/sync/sync_bind.c', 'ctx->cfg = cfg;\n    ctx->bind_cfg = cfg;', 'ctx->cfg = defaults();\n    ctx->bind_cfg = cfg;'),
    ('cfg-wildcard', 'gfx/sync/sync_cfg.c', "wildcard[9] = '*';", "wildcard[9] = '?';"),
    ('direct-submit-view', 'gfx/v2/submit.c',
     'v->sync = ctx;', 'v->sync = NULL;'),
    ('sync-publication', 'gfx/v2/backend_reg.c',
     '*mode = (u32)gfxsync_mode(*ctx);', '*mode = GFXSYNC_DIRECT;'),
]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    parser.add_argument('--host-only', action='store_true', help='run host composition without deployment-set validation')
    args = parser.parse_args()
    if args.host_only:
        print('HOST ONLY: deployment-set validation not performed', flush=True)
    else:
        deployment = json.loads((ROOT / 'build/out/deploy-set.json').read_text())
        paths = {e['guest'] for e in deployment['files']}
        assert {'/usr/bin/gfx2_i3.bin', '/etc/gfx/sync.cfg'} <= paths
    for mutation in [None, *MUTATIONS, None] if args.mutate else [None]:
        with tempfile.TemporaryDirectory(prefix='gfx2-i3-') as td:
            work = Path(td)
            cfg = (ROOT / 'assets/gfx/sync.cfg').read_bytes()
            if mutation and mutation[1] == 'assets/gfx/sync.cfg':
                cfg = cfg.replace(mutation[2].encode(), mutation[3].encode())
            (work / 'sync_defaults.inc').write_text(
                'static const u8 config_text[] = {' + ','.join(map(str, cfg)) + '};\n')
            observe(work, mutation, fixture='gfx2_i3_host.c', extra=EXTRA)

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    main()
