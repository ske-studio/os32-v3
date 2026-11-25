#!/usr/bin/env python3
"""Provider contract and old/new nonblocking keyboard selection traces."""
CHECK = dict(name='input-bridge-host', tier=1, budget=5, needs=[], covers=[
    'sys/hal/input/in2_int.h', 'sys/include/types.h', 'tools/tests/test_input_bridge.py',
    'tools/tests/host32.py', 'tools/tests/input_bridge_host.c',
    'sys/kern/input_legacy.c', 'sys/kern/input_wait.c', 'sys/platform/pc98/input/kbd.c'], basis=['INPUT_HAL_DESIGN §3', 'INPUT_HAL_DESIGN §5'])

import os
from pathlib import Path
import subprocess
import sys
import tempfile
from types import SimpleNamespace
import host32

ROOT = Path(__file__).resolve().parents[2]
# Selection functions from b27213d2:sys/platform/pc98/input/kbd.c; comments stripped.
OLD_KBD = r'''
static int kbd_gui_trygetbyte(void)
{
    u8 b = 0;
    if (!kbd_inject_take(&b)) return -1;
    return (int)b;
}
int kbd_trygetchar(void)
{
    u16 entry;
    kbd_foreground_poll();
    if (kbd_gui_mode) {
        int ch = kbd_gui_trygetbyte();
        if (ch >= 0) return ch;
        (void)exec_park_poll(tick_count);
        return -1;
    }
    if (rshell_active && !con_sink_capture_active()) {
        int sch;
        sch = serial_trygetchar();
        if (sch >= 0) return sch;
    }
    if (kbd_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);
    return (int)(entry & 0xFF);
}
int kbd_trygetchar_local(void)
{
    u16 entry;
    if (kbd_gui_mode) return -1;
    if (kbd_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);
    return (int)(entry & 0xFF);
}
int kbd_trygetkey(void)
{
    u16 entry;
    kbd_foreground_poll();
    if (kbd_gui_mode) {
        int ch = kbd_gui_trygetbyte();
        if (ch >= 0) return ch;
        (void)exec_park_poll(tick_count);
        return -1;
    }
    if (rshell_active && !con_sink_capture_active()) {
        int sch;
        sch = serial_trygetchar();
        if (sch >= 0) return sch;
    }
    if (kbd_count == 0) return -1;
    RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);
    return (int)entry;
}
int kbd_peekkey(void)
{
    u16 entry;
    unsigned int flags;
    if (kbd_gui_mode) {
        u8 b = 0;
        if (!kbd_inject_peek(&b)) return -1;
        return (int)b;
    }
    if (rshell_active && !con_sink_capture_active()) {
        int sch;
        sch = serial_peekchar();
        if (sch >= 0) return sch;
    }
    if (kbd_count == 0) return -1;
    flags = irq_save();
    entry = kbd_buf[kbd_head];
    irq_restore(flags);
    return (int)entry;
}
'''

# Wait oracle from 73c8664b; deadlines are left unchanged.
OLD_KBD += r'''
static int kbd_gui_getbyte(int interruptible)
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
int kbd_getkey_wait(int interruptible)
{
    u16 entry;
    u32 start_tick = tick_count;
    int capture = con_sink_capture_active();

    if (kbd_gui_mode) return kbd_gui_getbyte(interruptible);

    while (kbd_count == 0) {
        kbd_foreground_poll();
        if (interruptible && ring3_wait_pending()) return -1;
        idle_work_run();
        _halt();
        if (capture && (u32)(tick_count - start_tick) >= KBD_RSHELL_TIMEOUT_SEC * PIT_HZ)
            return 0x1b;
    }

    RING_DEQUEUE(entry, kbd_buf, kbd_head, kbd_count, KBD_BUF_SIZE);

    return (int)entry;
}
'''

FIXTURE = r'''
#include "in2_int.h"
#ifdef PPC_COMPILE
_Static_assert(sizeof(u32) == 4 && sizeof(void *) == 4, "PPC ILP32");
_Static_assert(sizeof(struct InputLegacyProvider) == 76, "provider size");
_Static_assert(__builtin_offsetof(struct InputLegacyProvider, wait_prepare) == 60, "appended prepare");
_Static_assert(__builtin_offsetof(struct InputLegacyProvider, park_kbd) == 36, "provider offset");
#endif
static int absent(const struct InputLegacyProvider *p) {
    return p && p->try_key ? p->try_key() : -1;
}
static int probe(void) {
    struct InputLegacyProvider p = {0};
    if (sizeof(p) != 19 * sizeof(void (*)(void))) return 1;
    if (IN2_MAX_DEV != 16 || IL_CHAR != 0 || IL_KEY != 1 || IL_LOCAL_CHAR != 2) return 2;
    if (absent(0) != -1 || absent(&p) != -1) return 3;
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) {
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
#else
int fixture(void) { return probe(); }
#endif
'''


def bridge_cases(ctx, tmp):
    kbd = (ctx.root / 'sys/platform/pc98/input/kbd.c').read_text()
    a = kbd.index('static int kbd_local_trykey(void)')
    b = kbd.index('#include "kbd_fgpoll.inc"', a)
    def function(name):
        start = kbd.index('int ' + name + '(void)\n{')
        return kbd[start:kbd.index('\n}', start) + 2]
    current = kbd[a:b] + '\n' + '\n'.join(function(n) for n in (
        'kbd_trygetchar', 'kbd_trygetkey', 'kbd_trygetchar_local', 'kbd_peekkey'))
    for name in ('kbd_getkey_wait',):
        start = kbd.index('int ' + name + '(int interruptible)\n{')
        current += '\n' + kbd[start:kbd.index('\n}', start)+2]
    start = kbd.index('static int kbd_gui_trygetbyte(void)')
    current = kbd[start:kbd.index('\n}', start)+2] + '\n' + current
    bridge = (ctx.root / 'sys/kern/input_legacy.c').read_text()
    bridge = bridge.replace('../input/hal/in2_int.h', 'in2_int.h')
    def execute(selection, product=None):
        (tmp / 'selected.inc').write_text(selection)
        (tmp / 'bridge.c').write_text(product or '')
        command = ['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
            '-static', '-no-pie', '-I' + str(tmp), '-I' + str(ctx.root / 'include'),
            str(ctx.root / 'tools/tests/input_bridge_host.c')]
        if product is not None:
            command += ['-DCURRENT', str(tmp / 'bridge.c'), str(ctx.root / 'sys/kern/input_wait.c')]
        subprocess.run(command + ['-o', str(tmp / 'trace')], check=True)
        result = host32.run([str(tmp / 'trace')], runner='qemu', capture_output=True, timeout=10)
        return result.returncode, result.stdout
    baseline = execute(OLD_KBD)
    assert baseline[0] == 0
    assert execute(current, bridge) == baseline
    old = 'return rshell_active && !con_sink_capture_active();'
    if ctx.mutate:
        assert current.count(old) == 1
        assert execute(current.replace(old, 'return rshell_active && (!con_sink_capture_active() || 1);', 1), bridge) != baseline
        print('RED runtime trace: capture serial gate (char/key)', flush=True)
        assert execute(current, bridge) == baseline
        wait = '    return input_legacy_wait(IL_KEY, interruptible);'
        assert current.count(wait) == 1
        mutant = current.replace(wait, '    (void)serial_trygetchar();\n' + wait, 1)
        assert execute(mutant, bridge)[0] == 6
        print('RED runtime: key wait reads serial', flush=True)
        assert execute(current, bridge) == baseline
    print('PASS old/new traces: 256 nonwaiting fixtures + 16 key waits', flush=True)
    if ctx.mutate:
        for old, new in [
            ('        return key;\n    }', '        /* fall through to serial/local */\n    }'),
            ('peek ? p->inject_peek : p->inject_try', 'p->inject_try'),
            ('p->park_poll();', '(void)0;'),
            ('if (mode == IL_LOCAL_CHAR) return -1;',
             'if (mode == IL_LOCAL_CHAR) { if (p->park_poll) p->park_poll(); return -1; }'),
            ('peek ? p->peek_key : p->try_key', 'p->try_key'),
            ('peek ? p->terminal_peek : p->terminal_try', 'p->terminal_try')]:
            assert bridge.count(old) == 1, old
            result = execute(current, bridge.replace(old, new, 1))
            assert result[0] in (0, 3, 7) and result != baseline, (old, result[0])
            print('RED runtime trace: ' + old.strip(), flush=True)
            assert execute(current, bridge) == baseline


def run(ctx):
    header = (ctx.root / 'sys/hal/input/in2_int.h').read_text()
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
    with tempfile.TemporaryDirectory(prefix='input-bridge-') as td:
        tmp = Path(td)
        (tmp / 'fixture.c').write_text(FIXTURE)
        def build(source):
            (tmp / 'in2_int.h').write_text(source)
            subprocess.run(['gcc', '-m32', '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib',
                '-static', '-no-pie', '-I' + str(tmp), '-I' + str(ctx.root / 'include'),
                str(tmp / 'fixture.c'), '-o', str(tmp / 'test')], check=True)
            return host32.run([str(tmp / 'test')], runner='qemu').returncode
        if build(header) != 0:
            return 1
        print('PASS x86 libc-free provider contract', flush=True)
        bridge_cases(ctx, tmp)
        if cc.is_file() and os.access(cc, os.X_OK):
            subprocess.run([str(cc), '-std=gnu11', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-DPPC_COMPILE', '-I' + str(tmp),
                '-I' + str(ctx.root / 'include'), '-c', str(tmp / 'fixture.c'),
                '-o', str(tmp / 'ppc.o')], check=True)
            print('PASS PowerPC cross compile', flush=True)
        else:
            print('SKIP PowerPC cross compile: toolchain unavailable', flush=True)
            if os.environ.get('OS32_REQUIRE_PPC') == '1':
                print('FAIL PowerPC toolchain required', flush=True)
                return 1
        if ctx.mutate:
            for old, new, expected in [
                ('    int (*terminal_enabled)(void);\n', '', 1),
                ('#define IN2_MAX_DEV 16', '#define IN2_MAX_DEV 8', 2)]:
                assert header.count(old) == 1
                rc = build(header.replace(old, new, 1))
                if rc != expected:
                    raise RuntimeError(f'mutation expected runtime {expected}, got {rc}')
                print('RED runtime: ' + old.strip(), flush=True)
    return 0


if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
