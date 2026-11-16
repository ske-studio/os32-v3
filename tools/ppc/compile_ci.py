#!/usr/bin/env python3
"""Stage 1 PPC image build: no emulator required."""
import os
from pathlib import Path
import shutil
import subprocess

IMAGES = (['KBOOT_SH=0', 'KBOOT_LINK=0'], ['KBOOT_SH=0', 'KBOOT_LINK=1'],
          ['KBOOT_SH=1', 'KBOOT_LINK=0'], ['KBOOT_SH=1', 'KBOOT_LINK=1'])


def run(root, env=None):
    env = dict(os.environ if env is None else env)
    cross = Path(env.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')).resolve()
    if shutil.which(str(cross / 'bin/powerpc-elf-gcc')) is None:
        required = env.get('OS32_REQUIRE_PPC') == '1'
        print(('FAIL' if required else 'SKIP') + ': PPC toolchain unavailable', flush=True)
        return 1 if required else 0
    for key in ('BUILD_OUT', 'KERNEL_CFLAGS', 'INC_KERNEL', 'C_STD',
                'SQLITE_PROTO_OUT', 'MAKEFLAGS', 'MFLAGS', 'MAKELEVEL'):
        env.pop(key, None)
    # Every image the PPC acceptance uses, with KBOOT_LINK explicit: the core
    # (kboot_accept/kboot_run), the link diagnostic, PLN1 SH and link-SH.
    for flags in IMAGES:
        result = subprocess.call(
            ['make', '-f', 'build/ppc-kboot.mk', *flags, 'PPC_CROSS_DIR=' + str(cross)],
            cwd=root, env=env)
        if result:
            return result
    return 0


if __name__ == '__main__':
    raise SystemExit(run(Path(__file__).resolve().parents[2]))
