"""Full real exec_launch nesting/normal-return, with real setjmp/longjmp."""
from pathlib import Path
import subprocess
import sys
import tempfile

import host32
from exec_source import exec_source
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
RESTORE = '        arch_fp_restore(&fp_parent[fp_depth]);\n        ring3_context_restore(&caller_context);\n        _enable();\n        io_syscall_resume(launcher_id, io_parent_depth); /* IO_SYSCALL_PARENT */\n        /* 畳み'
MUTATIONS = (
    ('focus-enter-omitted', 'av_focus_exec_enter(id);', ''),
    ('focus-leave-omitted', 'av_focus_exec_leave(id, res_owner_get());', ''),
    ('focus-fault-omitted', 'av_focus_exec_leave(id, res_owner_get());',
     'if (kind != EXEC_KIND_FAULT) av_focus_exec_leave(id, res_owner_get());'),
    ('focus-kill-omitted', 'av_focus_exec_leave(id, res_owner_get());',
     'if (kind != EXEC_KIND_ABORTED) av_focus_exec_leave(id, res_owner_get());'),
    ('nested-restore-zero', RESTORE, RESTORE.replace('[fp_depth]', '[0]')),
    ('nested-restore-previous', RESTORE,
     RESTORE.replace('[fp_depth]', '[fp_depth ? fp_depth - 1 : 0]')),
    ('normal-return-restore-missing', RESTORE,
     RESTORE.replace('arch_fp_restore(&fp_parent[fp_depth]);',
                     'if (fp_depth) arch_fp_restore(&fp_parent[fp_depth]);')),
    ('nested-save-previous', '        arch_fp_save(&fp_parent[fp_depth]);\n        arch_fp_init();',
     '        arch_fp_save(&fp_parent[fp_depth ? fp_depth - 1 : 0]);\n        arch_fp_init();'),
)


def run(source):
    with tempfile.TemporaryDirectory(prefix='x87-lifecycle-') as directory:
        tmp = Path(directory)
        (tmp / 'exec.c').write_text(source)
        obj = tmp / 'setjmp.o'
        result = subprocess.run(['nasm', '-f', 'elf32',
                                 'arch/x86/kernel/setjmp.asm', '-o', str(obj)],
                                cwd=ROOT, capture_output=True, text=True)
        if result.returncode:
            return 'compile', result.stderr
        exe = tmp / 'test'
        result = subprocess.run([
            'gcc', '-m32', '-std=gnu11', '-O2', '-Wall', '-Werror',
            '-Wno-unused-function', '-Wno-unused-but-set-variable',
            '-D__KERNEL_BUILD__', '-ffunction-sections', '-fdata-sections',
            '-Wl,--gc-sections', '-ffreestanding', '-fno-pie',
            '-fno-stack-protector', '-nostdlib', '-static', '-no-pie',
            '-I' + str(tmp), '-I' + str(ROOT / 'tools/tests/host_arch'),
            '-I' + str(ROOT / 'exec'), *include_flags(ROOT),
            str(ROOT / 'tools/tests/x87_lifecycle_host.c'), str(ROOT / 'kernel/av_focus.c'),
            str(obj), '-o', str(exe),
        ], capture_output=True, text=True)
        if result.returncode:
            return 'compile', result.stderr
        result = host32.run([str(exe)], capture_output=True, text=True, timeout=30)
        log = result.stdout + result.stderr
        if result.returncode == 0:
            return 'pass', log
        return ('runtime' if result.returncode == 1 and 'FAIL:' in log else 'unexpected'), log


if __name__ == '__main__':
    original = exec_source(ROOT)
    status, log = run(original)
    print(log, end='')
    if status != 'pass':
        raise SystemExit(status)
    if '--mutate' in sys.argv:
        for name, old, new in MUTATIONS:
            assert original.count(old) == 1, name
            status, log = run(original.replace(old, new))
            print(name + ': ' + status)
            if name == 'normal-return-restore-missing':
                assert 'PASS x87 nested:' in log, log
            if status != 'runtime':
                raise SystemExit(log or 'mutation must fail at runtime')
