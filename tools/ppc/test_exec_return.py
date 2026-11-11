#!/usr/bin/env python3
"""Exercise the PPC parent context across a simulated child longjmp."""
CHECK = dict(name='ppc-exec-return', tier=1,
             covers=['tools/ppc/test_exec_return.py'],
             inputs=['arch/ppc/exec_context.c', 'arch/ppc/arch_cpu.h',
                     'arch/ppc/arch_exec_storage.h', 'include/exec_arch.h',
                     'include/types.h', 'arch/ppc/ppc_frame.h',
                     'arch/ppc/exec_resume.S', 'lib/kstring.h', 'lib/kstring_c.c'], needs=[], budget=3)
import subprocess
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'arch/ppc/exec_context.c'
HARNESS = r'''
#include "exec_arch.h"
#include "ppc_frame.h"
extern u32 ppc_exec_kernel_sp;
static u32 root;
u32 ppc_root_current(void) { return root; }
void ppc_boot_load_root(u32 value) { root = value; }
static void *landing[5];
static void say(const char *s, int n) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static int check(void) {
    volatile struct arch_priv_context parent;
    root = 0x1234; ppc_exec_kernel_sp = 0x5678;
    arch_priv_save(&parent);
    if (!__builtin_setjmp(landing)) {
        root = 0xaaaa; ppc_exec_kernel_sp = 0xbbbb;
        __builtin_longjmp(landing, 1);
    }
    say("reached=parent\n", 15);
    arch_priv_restore(&parent);
    if (root != 0x1234) { say("FAIL: root\n", 11); return 5; }
    if (ppc_exec_kernel_sp != 0x5678) return 6;
    struct arch_user_context saved;
    u32 frame[ARCH_USER_BYTES / 4];
    struct arch_syscall call = { frame };
    for (unsigned i = 0; i < ARCH_USER_BYTES / 4; i++) frame[i] = 0x1000 + i;
    arch_user_save(&saved, &call);
    frame[3] = 0;
    arch_user_result(&saved, 0xabcdef01);
    arch_user_restart(&saved);
    u32 *words = (u32 *)saved.opaque;
    for (unsigned i = 0; i < ARCH_USER_BYTES / 4; i++) {
        u32 expected = i == 3 ? 0xabcdef01 : (i == PPC_FRAME_SRR0 / 4 ? 0x1000 + i - 4 : 0x1000 + i);
        if (words[i] != expected) return 7;
    }
    return 0;
}
void _start(void) {
    int rc = check();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''


def execute(source):
    with tempfile.TemporaryDirectory(prefix='ppc-return-') as directory:
        tmp = Path(directory)
        (tmp / 'context.c').write_text(source)
        (tmp / 'test.c').write_text(HARNESS)
        subprocess.run(['gcc', '-m32', '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-nostdlib', '-static', '-fno-stack-protector', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                        '-I' + str(ROOT / 'arch/ppc'), '-I' + str(ROOT / 'include'),
                        '-I' + str(ROOT / 'lib'), str(ROOT / 'lib/kstring_c.c'),
                        str(tmp / 'context.c'), str(tmp / 'test.c'),
                        '-o', str(tmp / 'test')], check=True)
        return subprocess.run(['qemu-i386', str(tmp / 'test')], capture_output=True, text=True)


def resume_check(source):
    # Interpret the actual restore instructions against a distinct saved frame.
    stop_translation = '''    mfmsr 3
    li 4,(PPC_MSR_IR|PPC_MSR_DR|PPC_MSR_RI)
    andc 3,3,4
    mtmsr 3
    isync'''
    if (source.count(stop_translation) != 1 or
            not source.index('bl ppc_boot_load_root') < source.index(stop_translation) < source.index('mtsprg1')):
        return False
    regs = [0] * 32
    regs[30] = 0x1000
    frame = [0x2000 + i for i in range(44)]
    spr = {}
    special = {}
    started = False
    srr_started = False
    for line in source.splitlines():
        line = line.strip()
        if line == 'lwz 0,PPC_FRAME_LR(30)':
            started = True
        if not started or not line or line.startswith(('#', '.', '/*')):
            continue
        op, *args = line.replace(',', ' ').split()
        if op == 'lwz':
            if srr_started:
                return False
            dst, addr = args
            off, base = addr.split('(')
            offsets = {'PPC_FRAME_LR': 128, 'PPC_FRAME_CR': 132,
                       'PPC_FRAME_CTR': 136, 'PPC_FRAME_XER': 140,
                       'PPC_FRAME_SRR0': 144, 'PPC_FRAME_SRR1': 148}
            offset = offsets[off] if off in offsets else int(off)
            regs[int(dst)] = frame[(regs[int(base[:-1])] + offset - 0x1000) // 4]
        elif op.startswith('mtsprg'):
            spr[op[-1]] = regs[int(args[0])]
        elif op.startswith('mfsprg'):
            regs[int(args[0])] = spr[op[-1]]
        elif op in ('mtlr', 'mtcr', 'mtctr', 'mtxer', 'mtsrr0', 'mtsrr1'):
            special[op] = regs[int(args[0])]
            if op.startswith('mtsrr'):
                srr_started = True
        elif op == 'rfi':
            return (regs == frame[:32] and
                    [special.get(k) for k in ('mtlr', 'mtcr', 'mtctr', 'mtxer', 'mtsrr0', 'mtsrr1')] == frame[32:38])
        else:
            return False
    return False


def main():
    source = SOURCE.read_text()
    result = execute(source)
    if result.returncode != 0 or result.stdout != 'reached=parent\n':
        print('baseline failed:', result.returncode, result.stdout, result.stderr)
        return 1
    asm = (ROOT / 'arch/ppc/exec_resume.S').read_text()
    if not resume_check(asm):
        print('resume baseline failed')
        return 1
    if '--mutate' in sys.argv:
        old = '    arch_mmu_load_root(root);'
        assert source.count(old) == 1
        result = execute(source.replace(old, '    (void)root;'))
        if result.returncode != 5 or result.stdout != 'reached=parent\nFAIL: root\n':
            print('mutation unexpected:', result.returncode, result.stdout, result.stderr)
            return 1
        print('parent root: RED (1)')
        for old, new in [('pc -= 4;', 'pc -= 8;'),
                         ('3 * sizeof(u32), &result', '4 * sizeof(u32), &result')]:
            assert source.count(old) == 1
            result = execute(source.replace(old, new))
            if result.returncode != 7:
                print('user mutation unexpected:', result.returncode)
                return 1
            print('user context: RED (1)')
        old = '    lwz 3,12(30)'
        assert asm.count(old) == 1
        if resume_check(asm.replace(old, '    lwz 3,16(30)')):
            print('resume mutation survived')
            return 1
        print('resume r3: RED (1)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
