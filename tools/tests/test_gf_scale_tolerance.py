"""Scale acceptance: output channel LSB boundary, clip and exact controls."""
CHECK = dict(name='gf-scale-tolerance-host', tier=1, needs=[], budget=30,
    covers=['tools/tests/test_gf_scale_tolerance.py', 'tools/tests/gf_scale_tolerance_host.c',
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
MUTATIONS = [('accept-two-lsb', 'kdebug/gf_accel_test.c',
            'if (d > 1U) return false;',
            'if (d > 2U) return false;'),
    ('observe-as-failure', 'kdebug/gf_accel_test.c',
     'return (observe_yuv ? spec_bad : rejected) ? GF2_EIO : 0;',
     'return rejected ? GF2_EIO : 0;'),
    ('observe-ignore-clip-corruption', 'kdebug/gf_accel_test.c',
     'if (observe_yuv && !scale_in_clip(i)) spec_bad++;',
     'if (observe_yuv && !scale_in_clip(i)) spec_bad += 0;'),
    ('observe-unbounded', 'kdebug/gf_accel_test.c',
     'if (shown >= TEST_SCALE_SAMPLES) continue;',
     'if (shown >= TEST_SCALE_SAMPLES + 1U) continue;'),
    ('observe-disconnected', 'kdebug/gf_accel_test.c',
     'bool observe_yuv = bilinear && !solid &&',
     'bool observe_yuv = false && bilinear && !solid &&'),
    ('observe-rgb-disconnected', 'kdebug/gf_accel_test.c',
     '(trial == 5U && fmt <= GF3_ARGB8888)',
     '(trial == 6U && fmt <= GF3_ARGB8888)'),
    ('observe-rgb-stride', 'kdebug/gf_accel_test.c',
     'x * cpp(scale_compare_fmt);', 'x * 4U;'),
    ('observe-rgb565-blue', 'kdebug/gf_accel_test.c',
     '(((g << 2) | (g >> 4)) << 8) | (b << 3) | (b >> 2);',
     '(((g << 2) | (g >> 4)) << 8) | (b << 3);'),
    ('observe-yuv-swap', 'kdebug/gf_accel_test.c',
     'number(" U=", p[yuy2 ? 1U : 0U]);',
     'number(" U=", p[yuy2 ? 3U : 2U]);'),
]


def command(exe):
    return ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
            '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
            *include_flags(ROOT), '-I'+str(ROOT),
            str(ROOT/'tools/tests/gf_scale_tolerance_host.c'), '-o', str(exe)]


def execute(exe, green, label):
    for runner in os.environ.get('HOST32_RUNNERS', 'qemu').split():
        rc = host32.run([str(exe)], runner=runner, cwd=ROOT).returncode
        print(f'{label}: runtime {"GREEN" if rc == 0 else "RED"} runner={runner} rc={rc}', flush=True)
        assert (rc == 0) == green, (label, runner, rc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='gf-scale-tolerance-') as tmp:
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
    return subprocess.call(['python3', '-B', 'tools/tests/test_gf_scale_tolerance.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)), close_fds=False)
