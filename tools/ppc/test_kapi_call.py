#!/usr/bin/env python3
"""Exercise the PPC logical KAPI boundary with a saved register frame."""
CHECK = dict(name='ppc-kapi-call', tier=1,
             covers=['arch/ppc/kapi_call.c', 'tools/ppc/test_kapi_call.py'],
             inputs=['arch/ppc/kapi_call.c', 'arch/ppc/ppc_frame.h',
                     'include/kapi_call.h', 'include/exec_arch.h',
                     'sdk/gen_kapi.py', 'sdk/kapi.json'], needs=[], budget=3)
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'tools/tests'))
import host32

HARNESS = r'''
#include "kapi_call.h"
#include "exec_arch.h"
#include "ppc_frame.h"
static int called;
static int fake(int a, int b) { called = a == 0x12 && b == 0x34; return -7; }
static void message(const char *s, int n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static int check(void) {
    struct ppc_frame frame = {0};
    struct arch_syscall call = {&frame};
    struct kapi_arg_source src;
    struct kapi_values values;
    frame.gpr[0] = 0; /* replaced with a two-integer slot by Python */
    frame.gpr[3] = 0x12; frame.gpr[4] = 0x34;
    arch_kapi_source(&src, &call);
    if (arch_syscall_slot(&call) != frame.gpr[0] ||
        !arch_kapi_read_block(&src, 0, 2, &values)) return 2;
    message("reached=invoke\n", 15);
    arch_syscall_result(&call, arch_kapi_invoke((void *)fake, &values, 2));
    if (!called || frame.gpr[3] != (u32)-7) {
        message("FAIL: arguments\n", 16); return 3;
    }
    return 0;
}
void _start(void) {
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''

def execute(source, tmp):
    (tmp / 'call.c').write_text(source)
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                    '-ffreestanding', '-fno-pie', '-no-pie', '-nostdlib', '-static',
                    '-fno-stack-protector', '-D__cdecl=', '-I' + str(tmp / 'gen/include'),
                    '-I' + str(ROOT / 'arch/ppc'), '-I' + str(ROOT / 'include'),
                    '-I' + str(ROOT / 'sdk/include/os32'), str(tmp / 'call.c'),
                    str(tmp / 'test.c'), '-o', str(tmp / 'test')], check=True)
    return host32.run([str(tmp / 'test')], capture_output=True, text=True)

def main():
    import json
    entries = json.loads((ROOT / 'sdk/kapi.json').read_text())['api']
    slot = next(i for i, item in enumerate(entries)
                if item['ret'] == 'int' and item.get('args') == ['int x', 'int y'])
    with tempfile.TemporaryDirectory(prefix='ppc-kapi-') as directory:
        tmp = Path(directory)
        subprocess.run([sys.executable, '-B', str(ROOT / 'sdk/gen_kapi.py'),
                        '--target', 'ppc-powermac', '--out-dir', str(tmp / 'gen'),
                        str(ROOT / 'sdk/kapi.json')], check=True, cwd=ROOT)
        (tmp / 'test.c').write_text(HARNESS.replace('frame.gpr[0] = 0;', f'frame.gpr[0] = {slot};'))
        source = (ROOT / 'arch/ppc/kapi_call.c').read_text()
        result = execute(source, tmp)
        if result.returncode != 0:
            print(result.stdout, result.stderr); return 1
        if '--mutate' in sys.argv:
            old = 'frame->gpr[3 + first + i]'
            assert source.count(old) == 1
            result = execute(source.replace(old, 'frame->gpr[4 + first + i]'), tmp)
            if result.returncode != 3 or result.stdout != 'reached=invoke\nFAIL: arguments\n':
                print('mutation unexpected:', result.returncode, result.stdout, result.stderr); return 1
            print('register offset: RED (1)')
    return 0

if __name__ == '__main__':
    sys.exit(main())
