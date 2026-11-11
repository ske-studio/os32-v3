#!/usr/bin/env python3
"""I2 composition with real lifecycle, drawing, synchronization and U1."""
import argparse
import json
import re
from pathlib import Path
import tempfile
from test_gfx2_i1 import observe


ROOT = Path(__file__).resolve().parents[2]
EXTRA = ['gfx/v2/' + p.name for p in (ROOT / 'gfx/v2').glob('sw_*.c') if p.stem != 'sw_table']
EXTRA += ['gfx/v2/' + n + '.c' for n in ['format', 'check_op', 'submit', 'v1_bridge']]
EXTRA += ['gfx/gfx_core.c']
EXTRA += ['userland/lib/gfx/v2/color.c', 'lib/divdi3.c']
MUTATIONS = [
    ('cui-owner', 'exec/appslot.c',
     '    if (gui_mode && (hdr_flags & OS32X_FLAG_GFX) == 0) return OS32_ERR_INVAL;',
     '    if (!gui_mode) return 0;\n    if ((hdr_flags & OS32X_FLAG_GFX) == 0) return OS32_ERR_INVAL;'),
    ('gart-view', 'gfx/v2/submit.c',
     'v->base = s->kva ? s->kva : (u8 *)(uptr)s->map_va;',
     'v->base = s->kva ? s->kva : (u8 *)(uptr)(s->map_va + MEM_PAGE_SIZE);'),
    ('adapter-heads', 'gfx/v2/backend_reg.c',
     'a->heads = a->caps.heads < 2 ? a->caps.heads : 2;', 'a->heads = 0;'),
    ('bridge-wait', 'gfx/v2/v1_bridge.c',
     'else rc = gfx2_cpu_begin(s, access, GFX2_BRIDGE_DL);', 'else rc = 0;'),
    ('mixed-fence', 'gfx/v2/submit.c',
     'return gfx2_fence_commit(a, f, ex.dst, ex.src);',
     'gfx2_fence_abandon(a, f); return 0;'),
    ('line-dispatch', 'gfx/v2/sw_table.c',
     '[GFX2_OP_LINE] = gfx2_sw_line,', '[GFX2_OP_LINE] = gfx2_sw_fill,'),
]

def stage_core(work, overrides):
    path = 'exec/appslot.c'
    body = overrides.get(path, (ROOT / path).read_text())
    functions = []
    for name in ['appslot_gfx_claim_check', 'appslot_gfx_claim', 'appslot_gfx_owner']:
        match = re.search(r'^int ' + name + r'\([^\n]*?\)\s*\{', body, re.M)
        assert match, name
        end, depth = match.end(), 1
        while depth:
            depth += (body[end] == '{') - (body[end] == '}')
            end += 1
        functions.append(body[match.start():end])
    (work / 'gfx2_i2_claim.inc').write_text('\n'.join(functions))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    deployment = json.loads((ROOT / 'build/out/deploy-set.json').read_text())
    assert any(entry['guest'] == '/usr/bin/gfx2_i2.bin' for entry in deployment['files'])
    for mutation in [None, *MUTATIONS, None] if args.mutate else [None]:
        with tempfile.TemporaryDirectory(prefix='gfx2-i2-') as td:
            observe(Path(td), mutation, fixture='gfx2_i2_host.c', extra=EXTRA, stage=stage_core)

def run(ctx):
    import subprocess
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)

if __name__ == '__main__':
    main()
