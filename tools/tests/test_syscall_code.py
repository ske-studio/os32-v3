"""H10: real ILP32 syscall emitter, all capacities, page bytes and publication."""
CHECK = dict(name='syscall-code-host', tier=1,
    covers=['include/syscall_code.h', 'arch/x86/kernel/syscall_code.c',
            'exec/exec_tramp_init.inc', 'exec/ring3_str.h', 'exec/ring3_ls.h',
            'arch/x86/kernel/ring3_ls.S', 'tools/tests/syscall_code_host.c',
            'tools/tests/test_syscall_code.py', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py'],
    needs=['make:kernel'], budget=3)
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from kernel_layout import linker_flags
ROOT = Path(__file__).resolve().parents[2]
CODE = 'arch/x86/kernel/syscall_code.c'
INIT = 'exec/exec_tramp_init.inc'
MUTATIONS = (
    ('interrupt-corrupted', CODE, 'st[6] = 0x80;', 'st[6] = 0x81;'),
    ('reserved-stubs-omitted', CODE, 'i < l.stub_count;', 'i < l.stub_count && i < KAPI_FUNC_COUNT;'),
    ('publish-omitted', INIT, 'arch_syscall_publish(P2V(page), layout.total_bytes);', '(void)page;'),
    ('emit-failure-ignored', INIT, 'if (!arch_syscall_emit(', 'if (0 && !arch_syscall_emit('),
)


def execute(changes=None):
    sources = {p: (ROOT / p).read_text() for p in (CODE, INIT)}
    if changes:
        name, path, old, new = changes
        assert sources[path].count(old) == 1, name
        sources[path] = sources[path].replace(old, new)
    with tempfile.TemporaryDirectory(prefix='syscall-code-') as folder:
        tmp = Path(folder)
        (tmp / 'code.c').write_text(sources[CODE])
        init = sources[INIT].split('static void ring3_trampoline_init(void)', 1)[1]
        (tmp / 'trampoline.inc').write_text('static void ring3_trampoline_init(void)' + init)
        exe = tmp / 'test'
        result = subprocess.run(['gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-no-pie', '-fno-stack-protector', '-nostdlib', '-static',
            '-D__KERNEL_BUILD__', '-I' + str(tmp), *include_flags(ROOT), *linker_flags(),
            str(ROOT / 'tools/tests/syscall_code_host.c'),
            str(ROOT / 'arch/x86/kernel/ring3_ls.S'), '-o', str(exe)], capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=30)
        return result.returncode, result.stdout + result.stderr


if __name__ == '__main__':
    rc, log = execute()
    print(log, end='')
    assert rc == 0, rc
    print('PASS: all capacities, unchanged page bytes, emit -> publish -> table; failure unpublished')
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
