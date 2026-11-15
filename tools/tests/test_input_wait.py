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
# a822e11c:drivers/input/kbd.c, character/GUI waits (comments stripped).
OLD_CHAR = r"""
static int old_gui(int interruptible)
{
    for (;;) {
        int ch = kbd_gui_trygetbyte();
        if (ch >= 0) return ch;
        (void)exec_park_kbd();
        if (interruptible && ring3_wait_pending()) return -1;
        idle_work_run();
        _halt();
    }
}
static int old_char(int interruptible)
{
    u16 entry;
    u32 timeout_ticks, start_tick;
    if (kbd_gui_mode) return old_gui(interruptible);
    timeout_ticks = (rshell_active || con_sink_capture_active())
                  ? KBD_RSHELL_TIMEOUT_SEC * PIT_HZ : 0;
    start_tick = tick_count;
    for (;;) {
        kbd_foreground_poll();
        if (kbd_count > 0) {
            RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);
            return (int)(entry & 0xFF);
        }
        if (rshell_active && !con_sink_capture_active()) {
            int sch = serial_trygetchar();
            if (sch >= 0) return sch;
        }
        if (interruptible && ring3_wait_pending()) return -1;
        idle_work_run();
        _halt();
        if (timeout_ticks > 0 && (u32)(tick_count - start_tick) >= timeout_ticks)
            return con_sink_capture_active() ? '\r' : ' ';
    }
}
"""

def run(ctx):
    source = (ctx.root / 'kernel/input_wait.c').read_text()
    with tempfile.TemporaryDirectory(prefix='input-wait-') as td:
        tmp = Path(td)
        kbd = (ctx.root / 'drivers/input/kbd.c').read_text()
        old = [OLD_CHAR]
        for signature in ('int kbd_getkey_wait(int interruptible)',):
            start = kbd.index(signature + '\n{')
            old.append(kbd[start:kbd.index('\n}', start) + 2])
        (tmp / 'old_wait.inc').write_text('\n'.join(old))
        a = kbd.index('static int kbd_local_trykey(void)')
        current = kbd[a:kbd.index('#include "kbd_fgpoll.inc"', a)]
        for name in ('kbd_gui_getbyte', 'kbd_getchar_wait'):
            a = kbd.index('static int ' + name + '(int interruptible)\n{')
            current += kbd[a:kbd.index('\n}', a)+2] + '\n'
        def execute(code, adapter=current):
            (tmp / 'current_wait.inc').write_text(adapter)
            (tmp / 'wait.c').write_text(code.replace('../input/hal/in2_int.h', 'in2_int.h'))
            cmd = ['gcc','-m32','-std=gnu11','-Wall','-Wextra','-Werror',
                   '-ffreestanding','-fno-pie','-fno-stack-protector','-nostdlib',
                   '-static','-no-pie','-I'+str(tmp),'-I'+str(ctx.root/'input/hal'),
                   '-I'+str(ctx.root/'include'),str(ctx.root/'tools/tests/input_wait_host.c'),
                   str(ctx.root/'kernel/input_legacy.c'),str(tmp/'wait.c'),'-o',str(tmp/'test')]
            subprocess.run(cmd, check=True)
            return host32.run([str(tmp/'test')], runner='qemu', timeout=10).returncode
        assert execute(source) == 0
        print('PASS libc-free wait: fixed oracle / real x86 provider, 960 traces', flush=True)
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
                ('gui = p->gui_active && p->gui_active();', 'gui = 0;'),
                ('if (gui) {', 'if (p->gui_active && p->gui_active()) {'),
                ('start = p->wait_ticks ? p->wait_ticks() : 0;', 'start = 100;'),
                ("mode == IL_KEY ? 0x1b : (p->capture_active && p->capture_active() ? '\\r' : ' ')", "mode == IL_KEY ? '\\r' : (capture ? 0x1b : ' ')"),
                ('if (mode == IL_CHAR && p->terminal_enabled &&', 'if (mode != IL_LOCAL_CHAR && p->terminal_enabled &&')]:
                assert source.count(old)==1
                assert execute(source.replace(old,new,1)) != 0
                print('RED runtime: '+old.strip(), flush=True)
                assert execute(source) == 0
            anchor = '.wait_prepare = kbd_foreground_poll'
            assert current.count(anchor) == 1
            assert execute(source, current.replace(anchor, '.wait_prepare = 0')) == 11
            print('RED runtime: x86 wait foreground poll disconnected', flush=True)
            assert execute(source) == 0
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
