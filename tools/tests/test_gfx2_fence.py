#!/usr/bin/env python3
"""Exercise C3 against a fake backend without libc."""
CHECK = dict(name='gfx2-fence-host', tier=1, needs=[], budget=30,
    covers=['gfx/v2/fence.c', 'gfx/v2/gfx2_int.h', 'tools/tests/gfx2_fence_host.c',
            'tools/tests/test_gfx2_fence.py', 'tools/tests/host32.py'])
from pathlib import Path
import argparse
import subprocess
import tempfile
import host32
ROOT = Path(__file__).resolve().parents[2]

def observe(work, source):
    (work / 'fence.c').write_text(source)
    (work / 'arch_cpu.h').write_text('void test_serialize(void);\n#define arch_cpu_serialize test_serialize\n')
    exe = work / 'test'
    dirs = [work, ROOT / 'gfx/v2', ROOT / 'gfx', ROOT / 'include', ROOT / 'mm', ROOT / 'arch/x86', ROOT / 'sdk/include/os32']
    host32.build(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O0', '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static', *['-I'+str(p) for p in dirs], str(work / 'fence.c'), str(ROOT / 'tools/tests/gfx2_fence_host.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)]).returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    source = (ROOT / 'gfx/v2/fence.c').read_text()
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        rc = observe(work, source)
        assert rc == 0, f'baseline runtime rc={rc}'
        print(f'GREEN C3 baseline: runtime rc=0 (runner={host32.selected_runner()})')
        if args.mutate:
            mutations = [
                ('wrap: use max instead of ordered waits',
                 '    rc = gfx2_fence_wait_k(a, s->last_write, dl_us);',
                 'rc = gfx2_fence_wait_k(a, s->last_write > s->last_read ? s->last_write : s->last_read, dl_us);', 16),
                ('wrap: equality is complete', '(i32)(completed - f) >= 0',
                 '(i32)(completed - f) > 0', 11),
                ('wrap: skip zero', 'if (!f) f = 1;', 'if (!f) f = 0;', 13),
                ('failure: only failed pending writes become LOST',
                 'reached(a->fence.failed_upto, s->last_write))',
                 '1)', 22),
                ('failure: recognize backend epoch loss',
                 'if (failed && failed != a->fence.failed_upto)',
                 'if (0 && failed != a->fence.failed_upto)', 21),
                ('failure: retain STALE after recovery',
                 'return a->fence.failed_upto && reached(a->fence.failed_upto, f);',
                 'return a->fence.failed_upto && !reached(a->fence.completed, f) && reached(a->fence.failed_upto, f);', 28),
                ('failure: reselect v1 policy', 'gfx_policy_reeval();',
                 '(void)0;', 21),
                ('failure: generation changes', 'a->gen++;', '(void)0;', 21),
                ('order: publish issued after fence_emit',
                 'int rc = a->be->fence_emit(a->ctx, f, GFX2_EMIT_DL);',
                 'int rc = (a->fence.issued = f, a->be->fence_emit(a->ctx, f, GFX2_EMIT_DL));', 33),
                ('order: record destination', 'if (dst) dst->last_write = f;',
                 'if (dst) dst->last_write = 0;', 33),
                ('order: record source', 'if (src) src->last_read = f;',
                 'if (src) src->last_read = 0;', 33),
                ('order: failed emit keeps issued unchanged',
                 'a->fence.failed_upto = f;',
                 'a->fence.failed_upto = a->fence.issued;', 35),
                ('order: recovery skips failed reservation',
                 'f = a->fence.failed_upto;', 'f = a->fence.issued;', 36),
                ('timeout: hold resources on stop failure',
                 'if (held || a->be->stop(a->ctx)) a->state = GFX2_AD_LOST_HELD;',
                 'if (held || a->be->stop(a->ctx)) a->state = GFX2_AD_LOST;', 41),
                ('cpu_end: VRAM serialization',
                 'if (s->pool == GFX2_POOL_VRAM) arch_cpu_serialize();',
                 'if (s->pool != GFX2_POOL_VRAM) arch_cpu_serialize();', 51),
                ('cpu_end: read does not flush',
                 'if (!(access & GFX2_CPU_W)) return;',
                 'if (!access) return;', 51),
                ('cpu_end: DIRECT flushes once', '(void)gfxsync_flush(ctx); return;',
                 '(void)ctx; return;', 52),
                ('cpu_end: SHADOW marks dirty', 'gfx2_dirty_add(s, r); return;',
                 '(void)r; return;', 53),
                ('sweep: periodically clear completed references',
                 'if ((f / GFX2_SWEEP_STEP) != (a->fence.last_sweep / GFX2_SWEEP_STEP))',
                 'if (0)', 61),
            ]
            for name, anchor, replacement, expected in mutations:
                assert source.count(anchor) == 1, (name, source.count(anchor))
                rc = observe(work, source.replace(anchor, replacement))
                assert rc == expected, f'{name}: runtime rc={rc}, expected {expected}'
                print(f'RED {name}: runtime rc={rc}')
            assert observe(work, source) == 0, 'final normal control failed'
            print(f'GREEN normal control after {len(mutations)} runtime RED mutants')
    print('PASS fence and CPU synchronization')

if __name__ == '__main__':
    main()

def run(ctx):
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
