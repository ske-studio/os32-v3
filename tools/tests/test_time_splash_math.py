"""PIT splash pure math, independent exit model and actual unconfigured P2."""
CHECK = dict(name='time-splash-math-host', tier=1,
    covers=['kernel/time/time_splash_math.c', 'kernel/time/time_splash_math.h',
            'kernel/time/ktime.c', 'kernel/time/ktime.h',
            'tools/tests/time_splash_math_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'include/types.h',
            'include/platform_timer.h', 'arch/x86/kernel/idt.h',
            'kernel/time/pit_math.h', 'kernel/time/time_math.h'], needs=[], budget=30.0)
import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HARNESS = 'tools/tests/time_splash_math_host.c'
PURE = 'kernel/time/time_splash_math.c'
KTIME = 'kernel/time/ktime.c'
MUTATIONS = (
    ('equal budget accepted', HARNESS, 'if (e.count <= c.budget) {', 'if (e.count < c.budget) {'),
    ('unset budget accepted', KTIME, 'if (!cost || cost->budget == 0)', 'if (!cost)'),
    ('lower range not updated', HARNESS, 'in.elapsed_min += a;', '(void)a;'),
    ('upper range not updated', HARNESS, 'in.elapsed_max += b;', '(void)b;'),
    ('loop bounds swapped', HARNESS, 'a += c.loop.lo + tries * c.retry.lo + c.head.lo;\n            b += c.loop.hi + tries * c.retry.hi + c.head.hi;', 'a += c.loop.hi + tries * c.retry.lo + c.head.lo;\n            b += c.loop.lo + tries * c.retry.hi + c.head.hi;'),
    ('restart from exit', HARNESS, 'in.end = next;', 'in.start = in.end; in.end = next;'),
    ('double tick adjustment', HARNESS, 'm->tick += (u32)m->result.tick_adjust;', 'm->tick += 2 * (u32)m->result.tick_adjust;'),
    ('pending sign', PURE, 'input->start.pending -\n                          input->end.pending', 'input->end.pending -\n                          input->start.pending'),
)

def execute(mutation=None, valid_loop=False):
    with tempfile.TemporaryDirectory(prefix='time-splash-') as td:
        work = Path(td)
        paths = {}
        for rel in (HARNESS, PURE, KTIME):
            source = (ROOT / rel).read_text()
            if mutation and mutation[1] == rel:
                assert source.count(mutation[2]) == 1, mutation[0]
                source = source.replace(mutation[2], mutation[3], 1)
            if rel == KTIME and (valid_loop or (mutation and mutation[0] == 'unset budget accepted')):
                # Exercise the same valid loop bound with and without the B guard.
                anchor = '.reload = SYSCLK_1997 / PIT_HZ }'
                assert source.count(anchor) == 1
                source = source.replace(anchor, '.reload = SYSCLK_1997 / PIT_HZ, .loop_min = 1 }', 1)
            dest = work / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(source)
            paths[rel] = dest
        # Product includes resolve against the copied source directory first.
        for rel in ('kernel/time/time_splash_math.h', 'kernel/time/ktime.h',
                    'kernel/time/time_math.h'):
            dest = work / rel
            dest.write_text((ROOT / rel).read_text())
        (work / 'arch').symlink_to(ROOT / 'arch', target_is_directory=True)
        (work / 'include').symlink_to(ROOT / 'include', target_is_directory=True)
        cc = str(Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc')
        exe = work / 'test'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-ffunction-sections', '-Wl,--gc-sections', '-D__cdecl=',
            '-I' + str(work), *include_flags(ROOT),
            *(str(paths[x]) for x in (HARNESS, PURE, KTIME)), '-lgcc', '-o', str(exe)], check=True)
        return host32.run([str(exe)], runner='qemu').returncode

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    assert execute() == 0
    assert execute(valid_loop=True) == 0
    print('GREEN PIT phases, pending, retry, exit accounting and unset P2', flush=True)
    if args.mutate:
        for mutation in MUTATIONS:
            rc = execute(mutation)
            assert rc > 0, (mutation[0], rc)
            print(f'RED {mutation[0]}: runtime rc={rc}', flush=True)

def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_time_splash_math.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root)
if __name__ == '__main__':
    main()
