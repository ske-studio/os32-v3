"""Execute the x86 context provider with observable TSS/resume hooks (ILP32)."""
CHECK = dict(name='exec-context-host', tier=1,
    covers=['include/types.h', 'include/callconv.h',
            'include/exec_arch.h', 'arch/x86/arch_exec_storage.h',
            'arch/x86/kernel/exec_context.c', 'arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'arch/x86/kernel/tss.h',
            'tools/tests/test_exec_context.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py'], needs=[], budget=2.0)
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HOST = r'''
#include "exec_arch.h"
#include "tss.h"
static u32 current_sp = 0x12345678, resumes;
struct tss_entry kernel_tss;
u32 tss_get_esp0(void) { return current_sp; }
void tss_set_esp0(u32 value) { current_sp = value; }
static void finish(int status) __attribute__((noreturn));
static void finish(int status) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}
#define CHECK(c) do { if (!(c)) finish(1); } while (0)
void ring3_resume(const u32 *frame, u32 root, void *tss) {
    CHECK(root == 0x345000 && tss == &kernel_tss);
    CHECK(frame[7] == 0x789 && frame[8] == 0x1000);
    resumes++;
}
/* PROVIDER */
void _start(void) {
    u32 native[13];
    for (unsigned k = 0; k < 13; k++) native[k] = 0x100 + k;
    struct arch_syscall call = { native };
    struct arch_user_context saved;
    volatile struct arch_priv_context parent;
    CHECK(sizeof(saved) == 52 && __alignof__(saved) == 4);
    CHECK(sizeof(parent) == 4 && __alignof__(parent) == 4);
    arch_priv_save(&parent);
    current_sp = 0xabcdef00;
    arch_priv_restore(&parent);
    CHECK(current_sp == 0x12345678);
    CHECK(arch_syscall_slot(&call) == 0x107);
    arch_syscall_result(&call, 0x789);
    CHECK(native[7] == 0x789);
    native[8] = 0x1002;
    for (unsigned pass = 0; pass < 2; pass++) {
        arch_user_save(&saved, &call);
        native[0] = 0x999; /* saved context owns values, never a live view */
        u32 *words = (u32 *)saved.opaque;
        CHECK(words[0] == 0x100);
        arch_user_result(&saved, 0x456);
        CHECK(words[7] == 0x456);
        arch_user_result(&saved, 0x789);
        arch_user_restart(&saved);
        CHECK(words[7] == 0x789 && words[8] == 0x1000);
        for (unsigned k = 1; k < 13; k++)
            if (k != 7 && k != 8) CHECK(words[k] == 0x100 + k);
        arch_user_resume(&saved, 0x345000);
        native[0] = 0x100;
    }
    CHECK(resumes == 2);
    finish(0);
}
'''
MUTATIONS = (
    ('priv-restore', '    tss_set_esp0(esp0);', '    (void)esp0;'),
    ('restart', '    eip -= 2;', '    eip -= 0;'),
    ('syscall-result', '((u32 *)call->native)[FRAME_EAX] = result;',
     '((u32 *)call->native)[FRAME_EAX] = result + 1;'),
    ('saved-result', '&result, sizeof(result));', '&result, 0);'),
)


def execute(source, folder):
    fixture = folder / 'context.c'
    fixture.write_text(HOST.replace('/* PROVIDER */', source))
    target = folder / 'context'
    subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra',
                    '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector',
                    '-nostdlib', '-static', '-no-pie', *include_flags(ROOT),
                    str(fixture), '-o', str(target)], check=True)
    return host32.run([str(target)], timeout=30).returncode


def main():
    source = (ROOT / 'arch/x86/kernel/exec_context.c').read_text()
    with tempfile.TemporaryDirectory(prefix='exec-context-') as temporary:
        folder = Path(temporary)
        assert execute(source, folder) == 0
        print('GREEN x86 context provider')
        if '--mutate' in sys.argv:
            for name, old, new in MUTATIONS:
                assert source.count(old) == 1, name
                assert execute(source.replace(old, new), folder) == 1, name
                print('runtime RED: ' + name)


def run(ctx) -> int:
    import os
    args = [sys.executable, '-B', 'tools/tests/test_exec_context.py']
    if ctx.mutate:
        args.append('--mutate')
    return subprocess.call(args, cwd=ctx.root,
                           env=dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners)))


if __name__ == '__main__':
    main()
