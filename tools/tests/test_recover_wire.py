"""T4b-1: real op 11 dispatcher, 40-byte wire rejection, no HAL calls."""
from pathlib import Path
import subprocess
import tempfile
import host32

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='recover-wire-') as directory:
        exe = Path(directory) / 'wire'
        includes = ('.', 'include', 'kernel', 'lib', 'fs', 'arch/x86', 'platform/pc98',
                    'sdk/include', 'sdk/include/os32', 'exec', 'gfx', 'drivers', 'kapi', 'kdebug')
        subprocess.run(['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall',
                        '-Wextra', '-Werror', '-ffreestanding', '-fno-pie',
                        '-fno-stack-protector', '-D__KERNEL_BUILD__', '-nostdlib',
                        '-static', '-no-pie', *['-I'+str(ROOT / x) for x in includes],
                        str(ROOT / 'tools/tests/recover_wire_host.c'),
                        str(ROOT / 'kdebug/kdebug_main.c'), '-o', str(exe)], check=True)
        result = host32.run([str(exe)], timeout=30)
        if result.returncode:
            return result.returncode
        print('recover wire: 40B/version/reserved/size/UNSUPPORTED/PAUSED PASS')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
