"""Layer 5 component hosts and the existing runner trace acceptance suite."""
CHECK = dict(name='gf5-host', tier=1, needs=[], budget=90.0,
             covers=['hwdev/gf/**', 'kdebug/gf5_test.c', 'kdebug/gfinit_pll.*',
                     'kdebug/gfinit_decode*', 'kdebug/gfinit_exec.c',
                     'kdebug/recover.c', 'kdebug/recover_gf.c', 'tools/tests/gf5*',
                     'tools/tests/test_gf5.py', 'tools/tests/test_gf_runner.py',
                     'tools/tests/gf_runner*', 'tools/tests/gf4_c2linit_host.c',
                     'tools/tests/gfinit_exec_host.c', 'tools/tests/gfinit_host.c',
                     'tools/tests/recover_gf_host.c', 'tools/tests/kernel_includes.py',
                     'tools/tests/host32.py',
                     'tools/tests/mutpar.py', 'tools/tests/gf4_display_fake.h',
                     'tools/tests/gf_accel_host.h', 'include/types.h'])

from pathlib import Path
import subprocess
import sys
import tempfile
import host32
import mutpar
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
# Each host defines GF5_HOST_UNIT before including gf5_fake.h.
CASES = {
    'perf': (0, ['hwdev/gf/perf.c']),
    'clock': (1, ['hwdev/gf/clock.c', 'kdebug/gfinit_pll.c']),
    'ramclock': (2, ['hwdev/gf/ramclock.c']),
    'therm': (3, ['hwdev/gf/therm.c']),
    'volt': (4, ['hwdev/gf/volt.c']),
    'tile': (5, ['hwdev/gf/tile.c']),
    'state': (6, ['hwdev/gf/state.c']),
    'rom': (8, ['hwdev/gf/feature5_rom.c']),
    'links': (9, ['hwdev/gf/feature5_links.c']),
}
# PM adds (name, product path, unique old text, new text, case) from each report.
MUTATIONS = []


def command(tree, case, exe):
    _, sources = CASES[case]
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
            '-Wextra', '-Werror', '-Werror=vla', '-Wno-unused-parameter',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector',
            '-ffunction-sections', '-fdata-sections', '-nostdlib', '-static',
            '-no-pie', '-Wl,--gc-sections', '-D__KERNEL_BUILD__',
            *include_flags(tree), '-I' + str(tree),
            str(tree / ('tools/tests/gf5_' + case + '_host.c')),
            *[str(tree / source) for source in sources], '-o', str(exe)]


def run_case(tree, case, directory):
    exe = directory / case
    subprocess.run(command(tree, case, exe), cwd=tree, check=True)
    return host32.run([str(exe)], cwd=tree, timeout=120).returncode


def main():
    failures = 0
    mutate = '--mutate' in sys.argv
    with tempfile.TemporaryDirectory(prefix='gf5-host-') as td:
        directory = Path(td)
        for case, (_, sources) in CASES.items():
            required = ['tools/tests/gf5_' + case + '_host.c', *sources]
            missing = [p for p in required if not (ROOT / p).is_file()]
            if missing:
                print(f'FAIL {case}: missing {", ".join(missing)}', flush=True)
                failures += 1
                continue
            try:
                rc = run_case(ROOT, case, directory)
            except subprocess.CalledProcessError:
                print(f'FAIL {case}: compile/link', flush=True)
                failures += 1
                continue
            if rc:
                print(f'FAIL {case}: runtime rc={rc}', flush=True)
                failures += 1
                continue
            print(f'PASS {case}: runtime GREEN', flush=True)
            if not mutate:
                continue
            for name, rel, old, new, target in MUTATIONS:
                if target != case:
                    continue
                original = (ROOT / rel).read_text()
                if not old or old == new or original.count(old) != 1:
                    print(f'FAIL {case}/{name}: invalid or ambiguous anchor', flush=True)
                    failures += 1
                    continue
                with tempfile.TemporaryDirectory(prefix='gf5-mut-') as md:
                    exe = Path(md) / case
                    cmd = command(ROOT, case, exe)
                    try:
                        tree = mutpar.build_in_tree(ROOT, md,
                            {rel: original.replace(old, new, 1)}, [cmd])
                        red = host32.run([str(exe)], cwd=tree, timeout=120).returncode
                    except subprocess.CalledProcessError:
                        print(f'FAIL {case}/{name}: compile/link is not runtime RED', flush=True)
                        failures += 1
                        continue
                    if red:
                        print(f'PASS {case}/{name}: runtime RED rc={red}', flush=True)
                    else:
                        print(f'FAIL {case}/{name}: survived', flush=True)
                        failures += 1
                try:
                    restored = run_case(ROOT, case, directory)
                except subprocess.CalledProcessError:
                    restored = 1
                if restored:
                    print(f'FAIL {case}/{name}: restored control', flush=True)
                    failures += 1
    runner = subprocess.run([sys.executable, '-B', str(ROOT / 'tools/tests/test_gf_runner.py'),
                             *(['--mutate'] if mutate else [])], cwd=ROOT)
    if runner.returncode:
        print(f'FAIL runner: rc={runner.returncode}', flush=True)
        failures += 1
    return int(bool(failures))


def run(ctx) -> int:
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    args = [sys.executable, '-B', 'tools/tests/test_gf5.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root, env=env, close_fds=False)


if __name__ == '__main__':
    raise SystemExit(main())
