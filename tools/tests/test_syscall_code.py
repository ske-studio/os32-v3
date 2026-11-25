"""H10: real ILP32 syscall emitter, all capacities, page bytes and publication."""
CHECK = dict(name='syscall-code-host', tier=1,
    covers=['sys/include/syscall_code.h', 'sys/arch/x86/kernel/syscall_code.c',
            'sys/platform/pc98/kernel_boot_memory.inc', 'sys/platform/powermac/ppc_kernel.c',
            'sys/kern/exec/exec_tramp_init.inc', 'sys/kern/exec/ring3_str.h', 'sys/kern/exec/ring3_ls.h',
            'sys/arch/x86/kernel/ring3_ls.S', 'tools/tests/syscall_code_host.c',
            'tools/tests/test_syscall_code.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py'],
    needs=['make:kernel'], budget=3,
    basis=['KAPI_SPEC §2', 'KAPI_SPEC §4'],
)
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from kernel_layout import linker_flags
ROOT = Path(__file__).resolve().parents[2]
CODE = 'sys/arch/x86/kernel/syscall_code.c'
INIT = 'sys/kern/exec/exec_tramp_init.inc'
PC98 = 'sys/platform/pc98/kernel_boot_memory.inc'
PPC = 'sys/platform/powermac/ppc_kernel.c'
MUTATIONS = (
    ('pc98-map-failure-ignored', PC98, 'paging_boot_user_shared(exec_tramp_page_addr(), 2) != 0',
     '(paging_boot_user_shared(exec_tramp_page_addr(), 2), 0)'),
    ('ppc-boot-continues', PPC, 'if (!shared_user) kboot_stop("KAPI trampoline");',
     'if (0 && !shared_user) kboot_stop("KAPI trampoline");'),
    ('interrupt-corrupted', CODE, 'st[6] = 0x80;', 'st[6] = 0x81;'),
    ('reserved-stubs-omitted', CODE, 'i < l.stub_count;', 'i < l.stub_count && i < KAPI_FUNC_COUNT;'),
    ('publish-omitted', INIT, 'arch_syscall_publish(P2V(page), RING3_TRAMP_BYTES);', '(void)page;'),
    ('layout-failure-ignored', INIT, 'if (!arch_syscall_layout(KAPI_FUNC_CAPACITY, &layout))',
     'if (!arch_syscall_layout(KAPI_FUNC_CAPACITY, &layout) && 0)'),
    ('emit-failure-ignored', INIT, 'if (!arch_syscall_emit(', 'if (0 && !arch_syscall_emit('),
)


def execute(changes=None):
    sources = {p: (ROOT / p).read_text() for p in (CODE, INIT, PC98, PPC)}
    if changes:
        name, path, old, new = changes
        assert sources[path].count(old) == 1, name
        sources[path] = sources[path].replace(old, new)
    with tempfile.TemporaryDirectory(prefix='syscall-code-') as folder:
        tmp = Path(folder)
        (tmp / 'code.c').write_text(sources[CODE])
        init = sources[INIT].split('int ring3_trampoline_init(void)', 1)[1]
        (tmp / 'trampoline.inc').write_text('int ring3_trampoline_init(void)' + init)
        pc = sources[PC98].split('    shm_init();', 1)[1].split('    /*', 1)[0]
        pm = sources[PPC].split('    shm_init();', 1)[1]
        pm = pm.split('#if KBOOT_SHELL || KBOOT_SH', 2)
        boot = 'static void pc98_boot(void) {\n' + pc + 'user_started++;\n}\n'
        for name, enabled in [('ppc_exec_boot', 1), ('ppc_gate_boot', 0)]:
            boot += (f'#define KBOOT_SHELL {enabled}\nstatic void {name}(void) {{\n'
                     + '#if KBOOT_SHELL || KBOOT_SH' + pm[1]
                     + 'user_started++;\n}\n#undef KBOOT_SHELL\n')
        (tmp / 'boot.inc').write_text(boot)
        exe = tmp / 'test'
        result = subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib', '-static',
            '-D__KERNEL_BUILD__', '-I' + str(tmp), *include_flags(ROOT), *linker_flags(),
            str(ROOT / 'tools/tests/syscall_code_host.c'),
            str(ROOT / 'sys/arch/x86/kernel/ring3_ls.S'), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=30)
        return result.returncode, result.stdout + result.stderr


if __name__ == '__main__':
    rc, log = execute()
    print(log, end='')
    assert rc == 0, rc
    print('PASS: all capacities, unchanged page bytes, emit -> table -> publish; failure unpublished')
    if '--mutate' in sys.argv:
        for mutation in MUTATIONS:
            rc, log = execute(mutation)
            assert rc > 0 and 'FAIL: syscall code' in log, (mutation[0], rc, log)
            print(mutation[0] + ': product line, compiled, runtime RED')


def run(ctx) -> int:
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_syscall_code.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
