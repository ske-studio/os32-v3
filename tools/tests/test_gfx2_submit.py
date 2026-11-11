#!/usr/bin/env python3
"""C9 stable batches, validation, dispatch and failure accounting, without libc."""
from pathlib import Path
import argparse
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = [
    ('batch-copy', 'rc = run_one(a, &execs[i]);',
     'struct gfx2_exec altered = execs[i]; altered.op = &ops[i]; rc = run_one(a, &altered);'),
    ('preflight-all', 'if (rc) goto out;\n    }\n    kmemset(&pattern',
     'if (rc) break;\n    }\n    kmemset(&pattern'),
    ('pattern-before-set', 'if ((o->flags & GFX2_F_PAT) && !pattern.kind)',
     'if ((o->flags & GFX2_F_PAT) && 0)'),
    ('pattern-mask', 'pat.color[i] &= mask;', 'pat.color[i] &= mask & 255u;'),
    ('capability', 'return e->how == GFX2_HOW_NOSUPPORT ? OS32_ERR_NOSYS : 0;',
     'e->how = GFX2_HOW_SW; return 0;'),
    ('extent-overflow', '(h - 1) > (~0UL - row) / pitch', '(h - 1) == 0xffffffffUL'),
    ('partial-count', 'k_done = i + 1;', 'k_done = i;'),
    ('abandon', 'gfx2_fence_abandon(a, f); return rc;', 'return rc;'),
    ('pattern-live-data', '(const void *)(uptr)o->data, pattern.kind == 2',
     '(const void *)(uptr)k_ops[0].data, pattern.kind == 2'),
]


def observe(work, source):
    path = work / 'submit.c'; path.write_text(source)
    exe = work / 'probe'
    flags = ['-I' + str(ROOT / p) for p in ('.', 'include', 'lib', 'sdk/include', 'sdk/include/os32', 'mm', 'gfx/v2')]
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0',
                  '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                  *flags, str(path), str(ROOT / 'gfx/v2/check_op.c'), str(ROOT / 'gfx/v2/sw_table.c'),
                  str(ROOT / 'tools/tests/gfx2_submit_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode


def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/submit.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, rc
        if args.mutate:
            for name, anchor, replacement in MUTATIONS:
                assert source.count(anchor) == 1, name
                rc = observe(work, source.replace(anchor, replacement))
                assert 0 < rc < 128, (name, rc)
                print(f'RED runtime {name}: rc={rc}')
    print('PASS C9 submit (real C8, fake SW/backend, qemu ILP32)')


if __name__ == '__main__':
    main()
