"""P2 ROM lifetime boundaries; reuse the freestanding ROM host and runner."""
CHECK = dict(name='gf5-rom-guard-host', tier=1, needs=[], budget=30.0,
             covers=['tools/tests/test_gf5_rom_guard.py', 'tools/tests/test_gf5.py',
                     'tools/tests/gf5_rom_host.c', 'tools/tests/gf5_fake.h',
                     'tools/tests/gf4_display_fake.h', 'tools/tests/gf_accel_host.h',
                     'tools/tests/host32.py', 'tools/tests/mutpar.py',
                     'tools/tests/kernel_includes.py', 'hwdev/gf/**', 'include/types.h',
                     # Inputs reachable through the shared test_gf5 runner.
                     'kdebug/gfinit_decode.c', 'kdebug/gfinit_exec.c',
                     'kdebug/gfinit_pll.c', 'kdebug/recover.c', 'kdebug/recover_gf.c',
                     'tools/tests/gf4_c2linit_host.c', 'tools/tests/gf_runner_host.c',
                     'tools/tests/gfinit_exec_host.c', 'tools/tests/gfinit_host.c',
                     'tools/tests/recover_gf_host.c', 'tools/tests/test_gf_runner.py'])

import subprocess
import sys
import test_gf5


MUTATIONS = [
    ('rom-acquire-guard', 'gf5_plan_invalidate(g); /* ROM acquire boundary */',
     '(void)g; /* ROM acquire boundary */'),
    ('rom-release-guard', 'gf5_plan_invalidate(g); /* ROM release boundary */',
     '(void)g; /* ROM release boundary */'),
    ('rom-acquire-generation',
     '++g->rom_generation; /* acquire: wrap is safe with the guard invalid */',
     '(void)g; /* acquire: generation omitted */'),
    ('rom-release-generation',
     '++g->rom_generation; /* release: never revive a guard on wrap */',
     '(void)g; /* release: generation omitted */'),
]


def main():
    # U7 owns test_gf5.py. Add P2 anchors here without editing its registry.
    test_gf5.MUTATIONS = [m for m in test_gf5.MUTATIONS if m[4] == 'rom'] + [
        (name, 'hwdev/gf/feature5_rom.c', old, new, 'rom')
        for name, old, new in MUTATIONS]
    sys.argv.extend(['--case', 'rom'])
    return test_gf5.main()


def run(ctx):
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_gf5_rom_guard.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
