#!/usr/bin/env python3
"""Native capture against the pinned pre-HAL keyboard; freestanding ILP32."""
CHECK = dict(name='input-native-host', tier=1, budget=10, needs=[], kind='characterization',
    covers=['drivers/input/kbd.c', 'drivers/input/kbd_hal.c', 'drivers/input/*.h',
            'input/hal/in2_int.h', 'input/hal/in2_dev.c', 'input/hal/keymap.h', 'input/hal/in2_state.c',
            'input/hal/kbd_merge.c', 'input/hal/in2_ring.c', 'input/hal/legacy_ring.c',
            'input/hal/legacy_mode.c', 'input/hal/mouse_ext.c', 'input/hal/pointer_state.c',
            'include/**', 'sdk/include/os32/**', 'tools/tests/host_arch/**',
            'platform/pc98/platform_io.h', 'build/config.mk', 'tools/tests/kernel_includes.py',
            'tools/tests/host32.py', 'tools/tests/test_input_native.py', 'tools/tests/input_native_host.c'])
from pathlib import Path
import re, subprocess, sys, tempfile
from types import SimpleNamespace
import host32
from kernel_includes import include_flags
ROOT = Path(__file__).resolve().parents[2]

def function(source, name):
    match = re.search(r'^(?:static )?(?:void|int) '+name+r'\([^;]*?\)\n\{', source, re.M)
    start, pos, depth = match.start(), match.end(), 1
    while depth:
        depth += (source[pos] == '{') - (source[pos] == '}')
        pos += 1
    return source[start:pos] + '\n'

def run(ctx):
    root = ctx.root
    def old(path):
        return subprocess.check_output(['git', 'show', '0bec13e7:'+path], cwd=root, text=True)
    kbd, ring, mode = [old(p) for p in ('drivers/input/kbd.c', 'input/hal/legacy_ring.c', 'input/hal/legacy_mode.c')]
    tables = kbd[kbd.index('static const u8 scancode_to_ascii['):kbd.index('/* ======================================================================== */\n/*  ロックキー')]
    prefix = ''.join('#define '+n+' oracle_'+n+'\n' for n in re.findall(r'^(?:void|int|u32) (\w+)\(', ring+'\n'+mode, re.M))
    prefix += ''.join('#define '+a+' '+b+'\n' for a,b in (
        ('kbd_gui_mode','oracle_gui'), ('kbd_stop_suppressed','oracle_stop'), ('kbd_shift_state','oracle_mods'),
        ('ring3_abort_request','oracle_abort_request'), ('legacy_kbd_push','oracle_guest_push'),
        ('legacy_request_exit','oracle_exit'), ('appslot_poll_yield_reset','oracle_poll_reset')))
    oracle = prefix + '''#include "drivers/input/kbd.h"
#include "in2_int.h"
#include "io.h"
#include "memmap.h"
int legacy_active(void);
void appslot_poll_yield_reset(void) {}
static u8 kbd_key_pressed[16];
volatile u8 kbd_shift_state;
int oracle_abort, oracle_guest;
void ring3_abort_request(void) { oracle_abort++; }
int legacy_kbd_push(u8 sc) { (void)sc; oracle_guest++; return 1; }
void legacy_request_exit(void) { oracle_guest++; }
''' + tables + ring + mode + ''.join(function(kbd,n) for n in ('kbd_lock_apply','kbd_deliver','kbd_is_pressed')) + '''
void oracle_init(u8 locks) { kbd_shift_state = locks; }
void oracle_deliver(u8 sc) { kbd_deliver(sc); }
int oracle_pressed(int sc) { return kbd_is_pressed(sc); }
void oracle_mode(int on) { kbd_set_gui_mode(on); }
void oracle_discard(void) { kbd_discard_stop(); }
int oracle_raw(void) { return legacy_raw_take(); }
int oracle_cooked(void) { return legacy_cooked_take(); }
u32 oracle_dropped(void) { return legacy_raw_dropped() + legacy_cooked_dropped(); }
'''
    paths = ['input/hal/'+n for n in ('in2_dev.c','in2_state.c','kbd_merge.c','in2_ring.c','mouse_ext.c','pointer_state.c','legacy_ring.c','legacy_mode.c')]
    paths += ['drivers/input/kbd_hal.c','drivers/input/kbd.c']
    sources = {p:(root/p).read_text() for p in paths}
    with tempfile.TemporaryDirectory(prefix='input-native-') as directory:
        work = Path(directory)
        def adapt(source):
            return source.replace('../../drivers/input/kbd.h',str(root/'drivers/input/kbd.h')).replace('../../input/hal/in2_int.h',str(root/'input/hal/in2_int.h'))
        (work/'oracle.c').write_text(adapt(oracle))
        def execute(texts):
            for path,source in texts.items(): (work/Path(path).name).write_text(adapt(source))
            (work/'native_deliver.inc').write_text(function(texts['drivers/input/kbd.c'],'kbd_deliver'))
            cmd = ['gcc','-m32','-std=gnu11','-Wall','-Wextra','-Werror','-ffreestanding','-fno-pie','-fno-stack-protector','-nostdinc','-nostdlib','-static','-no-pie',
                   '-I'+str(work),'-I'+str(root/'tools/tests/host_arch'),'-I'+str(root/'input/hal'),*include_flags(root)]
            subprocess.run(cmd+[str(work/Path(p).name) for p in paths[:-1]]+[str(work/'oracle.c'),str(root/'tools/tests/input_native_host.c'),'-o',str(work/'test')], check=True)
            return host32.run([str(work/'test')], runner='qemu').returncode
        assert execute(sources) == 0
        if ctx.mutate:
            for path,old_text,new in [
                ('drivers/input/kbd_hal.c','(void)input_capture(&event);','(void)input_capture(&event); (void)input_capture(&event);'),
                ('drivers/input/kbd_hal.c','.down = !(scancode & SCANCODE_BREAK)','.down = !!(scancode & SCANCODE_BREAK)'),
                ('drivers/input/kbd_hal.c','INPUT_ORIGIN_NATIVE','INPUT_ORIGIN_USB'),
                ('drivers/input/kbd.c','kbd_hal_capture(scancode);','(void)scancode;'),
                ('input/hal/kbd_merge.c','input_emit_legacy_key(sc, 1, mods);','(void)sc; (void)mods;')]:
                assert sources[path].count(old_text) == 1, old_text
                texts = dict(sources); texts[path] = texts[path].replace(old_text,new)
                rc = execute(texts); assert 0 < rc < 128, (old_text,rc)
                print('RED runtime: '+old_text, flush=True)
            assert execute(sources) == 0
        print('PASS native oracle: all scans/mods, GUI/CUI, repeat/full/STOP/V86/BIOS, one capture', flush=True)
    return 0
if __name__ == '__main__':
    sys.exit(run(SimpleNamespace(root=ROOT, mutate='--mutate' in sys.argv)))
