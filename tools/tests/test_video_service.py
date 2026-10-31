"""P0: fake devices + init contracts with the real instruction decoder."""
CHECK = dict(name='video-service-host', tier=1,
    covers=['tools/tests/video_service_host.c', 'tools/tests/gf_init_contract_host.c', 'tools/tests/host32.py', 'include/video_service.h', 'hwdev/gf/internal.h', 'kdebug/gfinit_decode.c', 'kdebug/gfinit_decode.h', 'kdebug/gfinit_decode/*.inc', 'kdebug/gfinit_op/*.inc', 'kdebug/gfinit_pll.c', 'kdebug/gfinit_pll.h', 'kdebug/gfinit_exec.h', 'kdebug/gfinit_pll/*.inc', 'hwdev/gf/scripts.h'],
    needs=[], budget=60.0)

import os
from pathlib import Path
import subprocess
import tempfile

import host32

ROOT = Path(__file__).resolve().parents[2]


def main():
    with tempfile.TemporaryDirectory(prefix='video-service-') as td:
        exe = Path(td) / 'host'
        subprocess.run([
            'gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
            '-Werror', '-Werror=vla', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
            *['-I' + str(ROOT / d) for d in
              ('.', 'include', 'sdk/include/os32', 'arch/x86', 'platform/pc98')],
            *[str(ROOT / f) for f in (
                'tools/tests/video_service_host.c',
                'tools/tests/gf_init_contract_host.c',
                'kdebug/gfinit_decode.c', 'kdebug/gfinit_pll.c')],
            '-o', str(exe)
        ], cwd=ROOT, check=True)
        for runner in os.environ.get('HOST32_RUNNERS', 'native qemu').split():
            result = host32.run([str(exe)], runner=runner, capture_output=True, text=True)
            assert result.returncode == 0 and result.stdout == '', result
            print('PASS video service: GF/Trident, REPEAT, wait/cancel, RESET restore, PLL commit (' + runner + ')')


if __name__ == '__main__':
    main()


def run(ctx) -> int:
    import os
    import shlex
    import subprocess
    base_env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    env = dict(base_env)
    argv = ['python3', '-B', 'tools/tests/test_video_service.py']
    rc = subprocess.call(argv, cwd=ctx.root, env=env, close_fds=False)
    if rc:
        return rc
    return 0
