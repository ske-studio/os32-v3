"""CENTER point-sample oracle: fixed half-pixel fixtures, one mutation."""
CHECK = dict(name='gf-scale-point-host', tier=1, needs=[], budget=3.4,
    covers=['tools/tests/recover_gf_dev_stub.c', 'tools/tests/test_gf_scale_point.py', 'tools/tests/gf_scale_point_host.c',
            'tools/tests/gf_command_runtime_host.h', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
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
MUTATION = ('point-floor', 'kdebug/gf_accel_test.c',
            'u32 point_bias = bilinear ? 0U : TEST_FRAC_ONE / 2U;',
            'u32 point_bias = 0U;')


def command(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
            *include_flags(ROOT), '-I'+str(ROOT),
            str(ROOT/'tools/tests/gf_scale_point_host.c'), str(ROOT/'tools/tests/recover_gf_dev_stub.c'), '-o', str(exe)]


def execute(exe, green, label):
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        rc = host32.run([str(exe)], runner=runner, cwd=ROOT).returncode
        print(f'{label}: runtime {"GREEN" if rc == 0 else "RED"} runner={runner} rc={rc}', flush=True)
        assert (rc == 0) == green, (label, runner, rc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-scale-point-') as tmp:
        directory = Path(tmp)
        exe = directory/'host'
        subprocess.run(command(exe), cwd=ROOT, check=True)
        execute(exe, True, 'control')
        if args.mutate:
            name, rel, old, new = MUTATION
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
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_scale_point.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
