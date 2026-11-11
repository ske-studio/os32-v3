#!/usr/bin/env python3
"""Inspect the assembled USER exception path and its busy-word mutation."""
CHECK = dict(name='ppc-user-vectors', tier=1, budget=10, needs=[],
             covers=['arch/ppc/kernel_vectors.S', 'tools/ppc/test_user_vectors.py'],
             inputs=['arch/ppc/ppc_frame.h', 'arch/ppc/ppc_oea.h'])
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CROSS = Path('/home/hight/opt/cross-ppc/bin')
SOURCE = ROOT / 'arch/ppc/kernel_vectors.S'


def inspect(source):
    for label in ('user_frame', 'save_frame', 'dispatch', 'restore_frame'):
        source = source.replace('.L' + label, 'test_' + label)
    with tempfile.TemporaryDirectory(prefix='ppc-user-vectors-') as directory:
        asm = Path(directory) / 'vectors.S'
        obj = Path(directory) / 'vectors.o'
        asm.write_text(source)
        subprocess.run([str(CROSS / 'powerpc-elf-gcc'), '-Wall', '-Wextra', '-Werror',
                        '-mcpu=7450', '-m32', '-mbig-endian',
                        '-I' + str(ROOT / 'arch/ppc'), '-c', str(asm), '-o', str(obj)], check=True)
        text = subprocess.check_output([str(CROSS / 'powerpc-elf-objdump'), '-dr', str(obj)], text=True)
    entry = text.split('<ppc_kernel_vector_entry>:', 1)[1].split('<test_user_frame>:', 1)[0]
    user = text.split('<test_user_frame>:', 1)[1].split('<test_save_frame>:', 1)[0]
    ret = text.split('<ppc_kernel_vector_return>:', 1)[1].split('<test_restore_frame>:', 1)[0]
    spill = text.split('<ppc_kernel_frame_complete>:', 1)[1].split('<test_dispatch>:', 1)[0]
    return (all(x in entry for x in ('andi.', '16384', '<test_user_frame>'))
            and 'ppc_exec_kernel_sp' in user and user.count('stw ') == 1
            and '0(r1)' in user and '<test_dispatch>' in spill
            and 'andi.' in spill and '<test_restore_frame>' in ret and 'andi.' in ret)


def main():
    source = SOURCE.read_text()
    if not inspect(source):
        raise RuntimeError('USER vector path incorrect')
    if '--mutate' in sys.argv:
        anchor = '.Luser_frame:\n    lis 1,ppc_exec_kernel_sp@ha'
        assert source.count(anchor) == 1
        mutant = source.replace(anchor, '.Luser_frame:\n    stw 29,0(28)\n    lis 1,ppc_exec_kernel_sp@ha')
        if inspect(mutant):
            raise RuntimeError('busy-word mutation survived')
        print('USER busy-word store: RED (1)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
