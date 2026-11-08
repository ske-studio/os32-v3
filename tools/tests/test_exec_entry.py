"""Run the real x86 entry/context providers without 32-bit libc."""
CHECK = dict(name='exec-entry-host', tier=1,
    covers=['arch/x86/kernel/exec_entry.c', 'arch/x86/kernel/exec_context.c',
            'arch/x86/kernel/tss.h', 'arch/x86/kernel/gdt.h', 'arch/x86/arch_exec_storage.h',
            'include/exec_arch.h', 'include/abi.h', 'include/types.h', 'include/cpu.h',
            'tools/tests/host_arch/*.h', 'tools/tests/exec_entry_host.c',
            'tools/tests/test_exec_entry.py', 'tools/tests/host32.py', 'tools/tests/kernel_includes.py'],
    needs=[], budget=3)
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]
MUTATIONS = (
    ('dummy-ret-omitted', '    if (store(owner, ctx.sp, user ? frame : frame + 1, user ? 20 : 16))',
     '    if (store(owner, ctx.sp + (user ? 4 : 0), frame + 1, 16))'),
    ('aux-api-swapped', '    frame[4] = args->aux;', '    frame[4] = args->api;'),
    ('underflow-rejection-omitted', '    if (top < 16) return -1;', '    if (0) return -1;'),
    ('aligned-underflow-rejection-omitted', '    if (user && ctx.sp < 4) return -1;', '    if (0) return -1;'),
    ('store-failure-ignored', '    if (store(owner, ctx.sp, user ? frame : frame + 1, user ? 20 : 16))',
     '    if ((store(owner, ctx.sp, user ? frame : frame + 1, user ? 20 : 16), 0))'),
    ('cpl0-aux-omitted', 'user ? 20 : 16)', 'user ? 20 : 12)'),
)
def execute(source=None):
    with tempfile.TemporaryDirectory(prefix='exec-entry-') as d:
        tmp=Path(d)
        (tmp/'entry.c').write_text(source if source is not None else (ROOT/'arch/x86/kernel/exec_entry.c').read_text())
        (tmp/'context.c').write_text((ROOT/'arch/x86/kernel/exec_context.c').read_text())
        exe=tmp/'test'
        p=subprocess.run(['gcc','-m32','-std=gnu11','-O2','-Wall','-Werror',
            '-Wno-unused-function','-Wno-unused-variable','-ffreestanding','-fno-pie',
            '-fno-stack-protector','-nostdlib','-static','-no-pie','-D__KERNEL_BUILD__',
            '-I'+str(tmp),'-I'+str(ROOT/'tools/tests/host_arch'),*include_flags(ROOT),
            str(ROOT/'tools/tests/exec_entry_host.c'),'-o',str(exe)],capture_output=True,text=True)
        if p.returncode: return 'compile',p.stderr
        p=host32.run([str(exe)],capture_output=True,text=True,timeout=30)
        log=p.stdout+p.stderr
        return ('pass' if p.returncode==0 else 'runtime' if 'FAIL:' in log else 'unexpected'),log
if __name__=='__main__':
    status,log=execute();print(log,end='')
    if status!='pass': raise SystemExit(status)
    if '--mutate' in sys.argv:
        original=(ROOT/'arch/x86/kernel/exec_entry.c').read_text()
        for name,old,new in MUTATIONS:
            assert original.count(old)==1,(name,original.count(old))
            status,log=execute(original.replace(old,new))
            print(name+': '+status)
            if status!='runtime': raise SystemExit(log or status)


def run(ctx) -> int:
    import os
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_exec_entry.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)
