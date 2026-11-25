#!/usr/bin/env python3
"""Calibrated spacing, MMIO write ordering, live command updates and sweep."""
CHECK = dict(name='trident-gap', tier=1, needs=[], budget=4.6,
    covers=['sys/include/copy_units.h', 'arch/*/arch_copy.h', 'sys/hal/gfx/sync/sync_int.h', 'sys/hal/gfx/v2/gfx2_int.h', 'sys/include/gfx_sync.h', 'sys/hal/gfx/gfx_hal2.h', 'sdk/include/os32/os32_gfx2.h', 'sys/dev/video/trident/regs.c', 'sys/dev/video/trident/present.c',
            'tools/tests/trident_sync_fake.c', 'sys/hal/gfx/sync/sync_int.h', 'sys/hal/gfx/v2/gfx2_int.h', 'drivers/video/trident/*.h',
            'sys/dev/video/trident/diag/trident_command_test.c', 'sys/dev/video/trident/diag/trident_command_test.h',
            'sys/dev/video/trident/diag/tri_command_wire.h', 'kdebug/generated/**',
            'tools/tests/trident_gap_host.c', 'tools/tests/test_trident_gap.py',
            'tools/tests/trident_gap_shell_host.c', 'userland/shell/cmd_tri.c',
            'userland/shell/shell.h',
            'tools/tests/host32.py', 'tools/tests/mutpar.py',
            'tools/tests/kernel_includes.py', 'include/*.h', 'gfx/*.h',
            'kernel/module/*.h', 'lib/serial_diag.h', 'sys/dev/char/serial.h',
            'sdk/include/os32/*.h', 'build/config.mk'],
    basis=['HAL_V2_DESIGN §10', 'TRIDENT_FULL_DESIGN'],
)
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree
ROOT = Path(__file__).resolve().parents[2]
GAP = 'sys/dev/video/trident/present.c'
REGS = 'sys/dev/video/trident/regs.c'
CMD = 'sys/dev/video/trident/diag/trident_command_test.c'
MUTATIONS = [
    ('command-update', CMD, 'o->set_present(d, TRI_PRESENT_DWORD_GAP, q->arg)', 'o->set_present(d, TRI_PRESENT_DWORD_GAP, 0)'),
    ('sweep-pattern', CMD, '            gap_invert(d);', '            (void)d;'),
    ('sweep-client-restore', CMD, 'if (inverted) gap_invert(d);', 'if (inverted) (void)d;'),
    ('sweep-count', CMD, 'frame < TRI_PRESENT_RUNS', 'frame < TRI_PRESENT_RUNS - 1u'),
    ('sweep-stop', CMD, 'if (!rc && bad) rc = TRI_E_ACCESS;', 'if (!rc && bad) return TRI_E_ACCESS;'),
    ('sweep-restore', CMD, '    o->clear_present(d);\n    d->write_gap_ns = saved_ns;', '    d->write_gap_ns = saved_ns;'),
    ('sweep-readback', CMD, 'if (actual != d->res->client[row * c->pitch + i]) ++bad;', 'if (actual == d->res->client[row * c->pitch + i]) ++bad;'),
]
def main():
    with tempfile.TemporaryDirectory(prefix='trident-gap-') as td:
        work=Path(td)
        for name,path,old,new in ([('normal',None,None,None)] +
                (MUTATIONS+[('restored',None,None,None)] if '--mutate' in sys.argv else [])):
            changes={}
            if old:
                s=(ROOT/path).read_text(); assert s.count(old)==1,(name,s.count(old))
                changes[path]=s.replace(old,new)
            exe=work/name
            command=['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
                '-ffreestanding','-fno-pie','-fno-stack-protector','-ffunction-sections',
                '-fdata-sections','-nostdlib','-static','-no-pie','-Wl,--gc-sections',
                '-I.','-Ikdebug',*include_flags(ROOT),
                'tools/tests/trident_gap_host.c',GAP,REGS,'tools/tests/trident_sync_fake.c',
                '-o',str(exe)]
            build_in_tree(ROOT,work/(name+'-tree'),changes,[command],capture_output=True)
            for runner in os.environ.get('HOST32_RUNNERS','qemu').split():
                result=host32.run([str(exe)],runner=runner,capture_output=True)
                assert result.returncode==(1 if old else 0),(name,result)
            print(('MUTATE RED ' if old else 'GREEN ')+name,flush=True)
        shim=work/'shim'; shim.mkdir()
        (shim/'string.h').write_text('int strcmp(const char *, const char *);\n')
        shell='userland/shell/cmd_tri.c'
        shell_mutants=[
            ('shell-set', 'req.test = TRI_TEST_GAP_SET;', 'req.test = TRI_TEST_GAP;'),
            ('shell-step', '!req.step_ns || req.arg > req.end_ns', 'req.arg > req.end_ns'),
        ]
        for name,old,new in ([('shell-normal',None,None)] +
                (shell_mutants+[('shell-restored',None,None)] if '--mutate' in sys.argv else [])):
            changes={}
            if old:
                src=(ROOT/shell).read_text(); assert src.count(old)==1
                changes[shell]=src.replace(old,new)
            exe=work/name
            cmd=['gcc','-m32','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
                '-ffreestanding','-fno-pie','-fno-stack-protector','-nostdlib',
                '-static','-no-pie','-I'+str(shim),'-I.','-Iinclude','-Isdk/include/os32',
                'tools/tests/trident_gap_shell_host.c','-o',str(exe)]
            build_in_tree(ROOT,work/(name+'-tree'),changes,[cmd],capture_output=True)
            for runner in os.environ.get('HOST32_RUNNERS','qemu').split():
                result=host32.run([str(exe)],runner=runner,capture_output=True)
                assert result.returncode==(1 if old else 0),(name,result)
            print(('MUTATE RED ' if old else 'GREEN ')+name,flush=True)
    return 0

def run(ctx):
    return subprocess.call(['python3','-B',__file__,*(['--mutate'] if ctx.mutate else [])],
        cwd=ctx.root,env=dict(os.environ,HOST32_RUNNERS=' '.join(ctx.runners)))
if __name__=='__main__':
    raise SystemExit(main())
