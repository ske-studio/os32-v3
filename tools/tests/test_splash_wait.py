"""Check actual frame deadlines against Python integer arithmetic."""
CHECK = dict(name='splash-wait-host', tier=1,
    covers=['platform/pc98/splash.c', 'lib/divdi3.c',
            'tools/tests/splash_wait_host.c', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py'], needs=[], budget=3.1)

import argparse
import os
from pathlib import Path
import random
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('round deadline down', 'platform/pc98/splash.c',
     'SPLASH_RASTER_LINE_NUM_US + den - 1) / den', 'SPLASH_RASTER_LINE_NUM_US) / den'),
    ('deadline one us late', 'platform/pc98/splash.c', 'if (now >= target)', 'if (now > target)'),
)


def vectors():
    rng = random.Random(0x6432)
    max64 = (1 << 64) - 1
    dens = [1, 2, 2483, 3147, (1 << 31) - 1, 1 << 31, (1 << 32) - 1]
    text = ['static const struct { u32 lines, den; unsigned long long start, now; int rc; } waits[] = {']
    pairs = [(lines, d) for lines in [0, 1, 400, 440, 525, 42949, 42950, (1 << 32) - 1]
             for d in [0, *dens]]
    pairs += [(rng.getrandbits(32), rng.randrange(1, 1 << 32)) for _ in range(128)]
    for lines, den in pairs:
        duration = ((lines or 400) * 100000 + (den or 2483) - 1) // (den or 2483)
        for start in [0, (1 << 32) - 1, (1 << 32), max64 - duration, max64 - duration + 1]:
            target = (start + duration) & max64
            for now in {start, target - 1, target, min(max64, target + 1)}:
                if now < 0:
                    continue
                rc = 0 if now >= start and now >= target else -1
                text.append(f'{{{lines}u, {den}u, {start}ULL, {now}ULL, {rc}}},')
    return '\n'.join([*text, '};'])


def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='splash-wait-') as td:
        work = Path(td)
        for rel in ['platform/pc98/splash.c', 'lib/divdi3.c']:
            source = (ROOT / rel).read_text()
            if mutation and rel == mutation[1]:
                assert source.count(mutation[2]) == 1, mutation[0]
                source = source.replace(mutation[2], mutation[3])
            dest = work / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(source)
        (work / 'splash_wait_vectors.h').write_text(vectors())
        exe = work / 'wait'
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        subprocess.run([cc, '-std=gnu11', '-march=i386', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
            '-D__KERNEL_BUILD__', '-I' + str(work), '-I' + str(work / 'lib'),
            *include_flags(ROOT), str(ROOT / 'tools/tests/splash_wait_host.c'),
            str(work / 'platform/pc98/splash.c'), str(work / 'lib/divdi3.c'),
            '-o', str(exe)], check=True)
        return host32.run([str(exe)], runner='qemu').returncode


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    assert execute() == 0
    print('GREEN frame rounding, defaults, 32/64-bit clock boundaries with shared helpers', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc > 0, (mutation[0], rc)
            print(f'RED {mutation[0]}: runtime rc={rc}', flush=True)


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_splash_wait.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)


if __name__ == '__main__':
    main()
