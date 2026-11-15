#!/usr/bin/env python3
"""Shared wait contract against the current x86 wait oracle."""
CHECK = dict(name='input-wait-host', tier=1, budget=5, needs=[], covers=[
    'input/hal/in2_int.h', 'include/types.h', 'kernel/input_legacy.c',
    'kernel/input_wait.c', 'tools/tests/input_wait_host.c',
    'tools/tests/test_input_wait.py', 'tools/tests/host32.py',
    'drivers/input/kbd.c'], kind='contract',
    basis=['INPUT_HAL_DESIGN §5', 'INPUT_HAL_DESIGN H7'])
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32
ROOT = Path(__file__).resolve().parents[2]

def run(ctx):
    source = (ctx.root / 'kernel/input_wait.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-wait-') as td:
        tmp = Path(td)
        kbd = (ctx.root / 'drivers/input/kbd.c').read_text()
        old = []
        for signature in ('static int kbd_getchar_wait(int interruptible)',
                          'int kbd_getkey_wait(int interruptible)'):
            start = kbd.index(signature + '\n{')
            old.append(kbd[start:kbd.index('\n}', start) + 2])
        (tmp / 'old_wait.inc').write_text('\n'.join(old))
        def execute(code):
            (tmp / 'wait.c').write_text(code.replace('../input/hal/in2_int.h', 'in2_int.h'))
            cmd = ['gcc','-m32','-std=gnu11','-Wall','-Wextra','-Werror',
                   '-ffreestanding','-fno-pie','-fno-stack-protector','-nostdlib',
                   '-static','-no-pie','-I'+str(tmp),'-I'+str(ctx.root/'input/hal'),
                   '-I'+str(ctx.root/'include'),str(ctx.root/'tools/tests/input_wait_host.c'),
                   str(ctx.root/'kernel/input_legacy.c'),str(tmp/'wait.c'),'-o',str(tmp/'test')]
            subprocess.run(cmd, check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        print('PASS x86 libc-free wait and existing wait oracle', flush=True)
        cc = Path(os.environ.get('PPC_CROSS_DIR','/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
        if cc.is_file() and os.access(cc, os.X_OK):
            for src in [ctx.root/'tools/tests/input_wait_host.c', ctx.root/'kernel/input_wait.c']:
                subprocess.run([str(cc),'-std=gnu11','-Wall','-Wextra','-Werror','-ffreestanding',
                    '-DPPC_COMPILE','-I'+str(tmp),'-I'+str(ctx.root/'input/hal'),'-I'+str(ctx.root/'include'),
                    '-c',str(src),'-o',str(tmp/(src.stem+'.o'))],check=True)
            print('PASS PowerPC compile', flush=True)
        else:
            print('SKIP PowerPC toolchain unavailable; x86 ran', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1': return 1
        if ctx.mutate:
            for old,new in [
                ('start = p->wait_ticks ? p->wait_ticks() : 0;', 'start = 100;'),
                ("mode == IL_KEY ? 0x1b : (capture ? '\\r' : ' ')", "mode == IL_KEY ? '\\r' : (capture ? 0x1b : ' ')"),
                ('if (mode == IL_CHAR && p->terminal_enabled &&', 'if (mode != IL_LOCAL_CHAR && p->terminal_enabled &&')]:
                assert source.count(old)==1
                assert execute(source.replace(old,new,1)) != 0
                print('RED runtime: '+old.strip(), flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
