#!/usr/bin/env python3
"""Calibrated spacing, MMIO write ordering, live command updates and sweep."""
CHECK = dict(name='trident-gap', tier=1, needs=[], budget=10,
    covers=['drivers/video/trident/regs.c', 'drivers/video/trident/present.c',
            'drivers/video/trident/write_gap.c', 'drivers/video/trident/*.h',
            'kdebug/trident_command_test.c', 'kdebug/trident_command_test.h',
            'kdebug/tri_command_wire.h', 'kdebug/generated/**',
            'tools/tests/trident_gap_host.c', 'tools/tests/test_trident_gap.py',
            'tools/tests/trident_gap_shell_host.c', 'userland/shell/cmd_tri.c',
            'userland/shell/shell.h',
            'tools/tests/host32.py', 'tools/tests/mutpar.py',
            'tools/tests/kernel_includes.py', 'include/*.h', 'gfx/*.h',
            'kernel/module/*.h', 'lib/serial_diag.h', 'drivers/char/serial.h',
            'sdk/include/os32/*.h', 'build/config.mk'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree
ROOT = Path(__file__).resolve().parents[2]
GAP = 'drivers/video/trident/write_gap.c'
REGS = 'drivers/video/trident/regs.c'
CMD = 'kdebug/trident_command_test.c'
STORE = '''        dst[i] = (u32)p[0] | ((u32)p[1] << 8) |
                 ((u32)p[2] << 16) | ((u32)p[3] << 24);
'''
LOOP = '''        u32 remaining = loops;
        while (remaining > 0) {
            __asm__ volatile("nop" ::: "memory");
            --remaining;
        }
'''
MUTATIONS = [
    ('conversion-scale', GAP, 'TRI_WRITE_GAP_TICK_NS << bit', '(TRI_WRITE_GAP_TICK_NS / 10u) << bit'),
    ('conversion-drop', GAP, 'd->write_gap_loops = loops;', 'd->write_gap_loops = 0; (void)loops;'),
    ('wait-before-write', REGS, STORE+LOOP, LOOP+STORE),
    ('skip-wait', REGS, 'u32 remaining = loops;', 'u32 remaining = 0; (void)loops;'),
    ('command-update', CMD, 'o->set_gap(d, q->arg);', 'o->set_gap(d, 0);'),
    ('sweep-pattern', CMD, '            gap_invert(d);', '            (void)d;'),
    ('sweep-client-restore', CMD, 'if (inverted) gap_invert(d);', 'if (inverted) (void)d;'),
    ('sweep-count', CMD, 'frame < TRI_PRESENT_RUNS', 'frame < TRI_PRESENT_RUNS - 1u'),
    ('sweep-stop', CMD, 'if (!rc && bad) rc = TRI_E_ACCESS;', 'if (!rc && bad) return TRI_E_ACCESS;'),
    ('sweep-restore', CMD, 'o->set_gap(d, saved_ns);', 'o->set_gap(d, 0); (void)saved_ns;'),
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
            # Observe each NOP at its production position; real volatile RAM
            # stores stay untouched. This makes a pre-store wait runtime RED.
            regs=changes.get(REGS,(ROOT/REGS).read_text())
            nop='__asm__ volatile("nop" ::: "memory");'
            assert regs.count(nop)==1
            changes[REGS]='extern void gap_observe(void);\n'+regs.replace(nop,'gap_observe();')
            exe=work/name
            command=['gcc','-m32','-march=i386','-std=gnu11','-O2','-Wall','-Wextra','-Werror',
                '-ffreestanding','-fno-pie','-fno-stack-protector','-ffunction-sections',
                '-fdata-sections','-nostdlib','-static','-no-pie','-Wl,--gc-sections',
                '-I.','-Ikdebug',*include_flags(ROOT),
                'tools/tests/trident_gap_host.c',GAP,REGS,'drivers/video/trident/present.c',
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
