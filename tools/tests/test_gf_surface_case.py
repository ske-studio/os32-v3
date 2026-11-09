"""Foreground transfer and scale surface alignment/row-stride regression."""
CHECK = dict(name='gf-surface-case-host', tier=1, needs=[], budget=3.6,
    covers=['tools/tests/recover_gf_dev_stub.c', 'tools/tests/test_gf_surface_case.py', 'tools/tests/gf_surface_case_host.c',
            'tools/tests/gf_command_runtime_host.h', 'tools/tests/gf_command_host.h',
            'tools/tests/host32.py', 'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
            'kdebug/gf_accel_test.c', 'kdebug/*.h', 'hwdev/gf/*.h',
            'hwdev/gf/display/*.h', 'hwdev/*.h', 'include/**', 'mm/*.h',
            'gfx/*.h', 'drivers/video/geforce/*.h', 'drivers/pci/*.h',
            'lib/serial_diag.h', 'lib/crc32_core.inc', 'kernel/irq/irq.h'])

import argparse
import os
from pathlib import Path
import subprocess
import tempfile
import host32
import mutpar
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]


def command(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
            *include_flags(ROOT), '-I'+str(ROOT),
            str(ROOT/'tools/tests/gf_surface_case_host.c'), str(ROOT/'tools/tests/recover_gf_dev_stub.c'), '-o', str(exe)]


def execute(exe, green, label):
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        rc = host32.run([str(exe)], runner=runner, cwd=ROOT).returncode
        assert (rc == 0) == green, (label, runner, rc)
        print(f'{label}: runtime {"GREEN" if green else "RED"} runner={runner} rc={rc}', flush=True)


MUTATIONS = [
    ('transfer-old-pitch', 'kdebug/gf_accel_test.c',
     'fmt, 0, w, h, gpu_pitch)', 'fmt, 0, w, h, rowbytes)'),
    ('scale-old-pitch', 'kdebug/gf_accel_test.c',
     '16, 16, TEST_SCALE_SRC_PITCH)', '16, 16, 16U * cpp(fmt))'),
    ('scale-old-seed', 'kdebug/gf_accel_test.c',
     'expected + y * TEST_SCALE_SRC_PITCH + x * cpp(fmt)',
     'expected + (y * 16U + x) * cpp(fmt)'),
    ('scale-old-reader', 'kdebug/gf_accel_test.c',
     'before + y * TEST_SCALE_SRC_PITCH + x * cpp(fmt)',
     'before + y * 16U * cpp(fmt) + x * cpp(fmt)'),
    ('scale-old-yuv-reader', 'kdebug/gf_accel_test.c',
     'before + y * TEST_SCALE_SRC_PITCH + (x & ~1U) * 2U',
     'before + y * 32U + (x & ~1U) * 2U'),
    ('transfer-old-oracle', 'kdebug/gf_accel_test.c',
     'expected[y * gpu_pitch + b] = transfer',
     'expected[y * rowbytes + b] = transfer'),
]



def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-surface-case-') as tmp:
        directory = Path(tmp)
        exe = directory/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        execute(exe, True, 'control')
        if args.mutate:
            for name, rel, old, new in MUTATIONS:
                source = (ROOT/rel).read_text()
                assert source.count(old) == 1, name
                mutpar.build_in_tree(ROOT, directory/name,
                    {rel: source.replace(old, new, 1)}, [command(exe)])
                execute(exe, False, name)
            subprocess.run(command(exe), cwd=ROOT, check=True)
            execute(exe, True, 'restored')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())


def run(ctx):
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_surface_case.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
