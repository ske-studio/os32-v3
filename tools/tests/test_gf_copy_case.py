"""Foreground copy table: legal EXA pitches, pixels and pre-fault inputs."""
CHECK = dict(name='gf-copy-case-host', tier=1, needs=[], budget=30,
    covers=['tools/tests/test_gf_copy_case.py', 'tools/tests/gf_copy_case_host.c',
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
            str(ROOT/'tools/tests/gf_copy_case_host.c'), '-o', str(exe)]


def execute(exe, green, label):
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        rc = host32.run([str(exe)], runner=runner, cwd=ROOT).returncode
        assert (rc == 0) == green, (label, runner, rc)
        print(f'{label}: runtime {"GREEN" if green else "RED"} runner={runner} rc={rc}', flush=True)


MUTATIONS = [
    ('original-11th-pitch', 'kdebug/gf_accel_test.c',
     '#define TEST_COPY_DST_PITCH (3U * 64U)', '#define TEST_COPY_DST_PITCH 40U'),
    ('missing-copy-input', 'kdebug/gf_accel_test.c',
     'serial_puts_polled("GF3 copy input");', 'serial_puts_polled("GF3 wrong input");'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-copy-case-') as tmp:
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
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_copy_case.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
