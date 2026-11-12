"""Exercise the real OS32X admission predicate with the PPC target facade."""
CHECK = dict(covers=['arch/ppc/arch_exec_image.h', 'arch/x86/arch_exec_image.h', 'exec/os32x_hdr.c'],
             inputs=['tools/ppc/test_exec_target.py'])
from pathlib import Path
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[2]
SOURCE = r'''
#include "os32x_hdr.h"
int main(void) {
    OS32Header h = {0};
    h.magic = OS32X_MAGIC; h.version = OS32X_HDR_VERSION;
    h.header_size = OS32X_HDR_SIZE; h.kapi_data_off = KAPI_DATA_FIELDS_OFF;
    h.kapi_abi_generation = OS32_KAPI_ABI_GENERATION;
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    h.flags = OS32X_FLAG_PPC32;
    if (os32x_layout_check(&h, OS32X_HDR_SIZE, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_OK) return 1;
    h.flags = 0;
    if (os32x_layout_check(&h, OS32X_HDR_SIZE, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_OLD) return 2;
    return 0;
}
'''

def check(mutant):
    with tempfile.TemporaryDirectory(prefix='ppc-exec-target-') as directory:
        tmp = Path(directory)
        header = (ROOT / 'arch/ppc/arch_exec_image.h').read_text()
        if mutant:
            old = '(flags & OS32X_FLAG_PPC32) != 0'
            assert header.count(old) == 1
            header = header.replace(old, '(flags & OS32X_FLAG_PPC32) == 0')
        (tmp / 'arch/ppc').mkdir(parents=True)
        (tmp / 'arch/ppc/arch_exec_image.h').write_text(header)
        (tmp / 'main.c').write_text(SOURCE)
        exe = tmp / 'test'
        cmd = ['gcc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-D__cdecl=',
               '-I' + str(tmp / 'arch/ppc'), '-I' + str(ROOT / 'include'), '-I' + str(ROOT / 'arch/ppc'),
               '-I' + str(ROOT / 'exec'), '-I' + str(ROOT / 'sdk/include/os32'),
               str(ROOT / 'exec/os32x_hdr.c'), str(tmp / 'main.c'), '-o', str(exe)]
        subprocess.run(cmd, check=True)
        result = subprocess.run([str(exe)], check=False)
        expected = 1 if mutant else 0
        if result.returncode != expected:
            raise SystemExit('unexpected runtime status: ' + str(result.returncode))
        print('mutant RED' if mutant else 'PPC target PASS')

if __name__ == '__main__':
    check(False)
    if '--mutate' in sys.argv:
        check(True)
