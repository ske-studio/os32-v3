"""KDEBUG=0 production startup, resource gate, cleanup and reboot order."""
from pathlib import Path
import os
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def main():
    flags = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
             '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
             '-fno-stack-protector', '-ffunction-sections', '-fdata-sections',
             '-DKDEBUG=0', '-DGF_PRODUCTION',
             *['-I'+str(ROOT / p) for p in ('.', 'include', 'kernel', 'lib', 'fs',
                 'sdk/include/os32', 'exec', 'drivers', 'arch/x86', 'platform/pc98')]]
    with tempfile.TemporaryDirectory(prefix='gf-production-') as tmp:
        exe = Path(tmp) / 'host'
        native = Path(tmp) / 'native.o'
        # P5 owns module/build registration. Compile the new production TUs
        # with the actual cross compiler too, while that parallel wiring lands.
        cross = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        for name in ('devinit', 'bus', 'native', 'resources'):
            subprocess.run([str(cross), *flags, '-D__KERNEL_BUILD__', '-c',
                            str(ROOT / f'hwdev/gf/{name}.c'),
                            '-o', str(Path(tmp) / f'{name}-cross.o')], check=True)
        subprocess.run(['gcc', *flags, '-Dgf_production_mmio=gf_production_native_mmio',
                        '-c', str(ROOT / 'hwdev/gf/native.c'), '-o', str(native)], check=True)
        sources = ['tools/tests/gf_production_host.c', 'kernel/sys.c',
                   'hwdev/gf/devinit.c', 'hwdev/gf/bus.c', 'hwdev/gf/resources.c',
                   'hwdev/gf/runner.c', 'hwdev/gf/save.c', 'hwdev/gf/pll.c',
                   'hwdev/gf/service.c', 'hwdev/gf_load_state.c', 'kdebug/gfinit_resources.c']
        sources += [str(p.relative_to(ROOT)) for pattern in ('hwdev/gf/op/*.c', 'hwdev/gf/display/*.c')
                    for p in sorted(ROOT.glob(pattern))]
        subprocess.run(['gcc', *flags, '-nostdlib', '-static', '-no-pie', '-Wl,--gc-sections',
                        *[str(ROOT / p) for p in sources], str(native), '-o', str(exe)], check=True)
        symbols = subprocess.check_output(['nm', '--defined-only', str(exe)], text=True)
        assert not any(name in symbols for name in (' recover_', ' kpeek_', ' trial_'))
        return host32.run([str(exe)], cwd=ROOT, timeout=180).returncode


if __name__ == '__main__':
    raise SystemExit(main())
