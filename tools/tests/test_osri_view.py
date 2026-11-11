"""OSRI KAPI: real entry, loader, owner, HAL, deadline and VBLANK poll."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from test_splash_wire import osri

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['tools/tests/gfx2_bridge_fake.h', 'tools/tests/osri_view_host.c', 'tools/tests/splash_raster_host.c',
           'gfx/gfx_osri.c', 'gfx/gfx_osri_view.c', 'gfx/gfx_osri_view.h', 'gfx/gfx_internal.h',
           'exec/appslot.h', 'exec/exec.h',
           'kapi/kapi_osri.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pc98_osri.c',
           'drivers/video/pc98/splash_raster.c', 'drivers/video/pc98/splash_raster_play.c',
           'drivers/video/pc98/splash_raster_plan.c', 'drivers/video/pc98/splash_raster_run.c',
           'drivers/video/pc98/splash_raster_run.h', 'drivers/video/pc98/splash_raster_play.h',
           'lib/le_internal.h', 'lib/crc32_core.inc']
# Each tuple's last member names the runtime assertion group it must kill.
MUTATIONS = (
    ('omit copy', 'kapi/kapi_osri.c',
     '!copy_caller_bytes(&caller, path + i, copy + i, 1)',
     '(copy[i] = path[i], 0)', 'entry_and_failures'),
    ('omit rebase', 'drivers/video/pc98/splash_raster_run.c',
     'r->start = now;', '(void)now;', 'deadlines'),
    ('stack extensions', 'drivers/video/pc98/splash_raster_run.c',
     'remaining > extension_us ? remaining : extension_us',
     'remaining + extension_us', 'deadlines'),
    ('double consume', 'drivers/video/pc98/backend_pc98_osri.c',
     'int key = input_trygetkey();',
     'int key = input_trygetkey(); (void)input_trygetkey();', 'deadlines'),
    ('ignore abort', 'drivers/video/pc98/backend_pc98_osri.c',
     'if (gfx_osri_poll_abort(p->caller)) return 1;',
     'if (0 && gfx_osri_poll_abort(p->caller)) return 1;', 'deadlines'),
    ('ignore Esc', 'drivers/video/pc98/backend_pc98_osri.c',
     'if ((key & 0xff) == OSRI_ESC_ASCII) return 1;',
     'if (0 && (key & 0xff) == OSRI_ESC_ASCII) return 1;', 'deadlines'),
    ('GUI inject only', 'drivers/video/pc98/backend_pc98_osri.c',
     'int gui = con_sink_is_enabled();', 'int gui = 0;', 'gui_input'),
    ('ignore stop pending', 'gfx/gfx_osri_view.c',
     '|| appslot_stop_pending()', '|| (0 && appslot_stop_pending())', 'gui_input'),
    ('omit owner', 'gfx/gfx_osri_view.c',
     '(con_sink_is_enabled() && caller != appslot_gfx_owner())',
     '(0 && con_sink_is_enabled() && caller != appslot_gfx_owner())', 'entry_and_failures'),
    ('omit route', 'drivers/video/pc98/backend_pc98_osri.c',
     '(image->count && pl_run_timing(state))',
     '(0 && image->count && pl_run_timing(state))', 'entry_and_failures'),
    ('late poll', 'drivers/video/pc98/splash_raster_play.c',
     'if (poll && poll(ctx)) return PL_FRAME_CANCEL;\n    unsigned int flags = irq_save();',
     'unsigned int flags = irq_save();\n    if (poll && poll(ctx)) { irq_restore(flags); return PL_FRAME_CANCEL; }',
     'boundaries_and_fixed'),
    ('ignore poll cancel', 'drivers/video/pc98/splash_raster_play.c',
     'if (poll && poll(ctx)) return PL_FRAME_CANCEL;',
     'if (poll && poll(ctx)) { /* continue visible scan */ }', 'boundaries_and_fixed'),
)

def execute(mutation=None):
    with tempfile.TemporaryDirectory(prefix='osri-view-') as td:
        work = Path(td)
        for rel in SOURCES:
            dst = work/rel; dst.parent.mkdir(parents=True, exist_ok=True)
            source = (ROOT/rel).read_text()
            if mutation and mutation[1] == rel:
                name, _, old, new, _ = mutation
                assert source.count(old) == 1, name
                source = source.replace(old, new, 1)
            if rel == 'tools/tests/splash_raster_host.c':
                source = source.replace('static unsigned int inp(unsigned int port)\n{',
                                        'static unsigned int inp(unsigned int port)\n{\n    advance();')
            dst.write_text(source)
        data = [('fixed', osri()), ('events', osri(True)), ('budget', osri(2))]
        bad = bytearray(osri(True)); bad[96] ^= 1
        data.append(('bad_crc', bad))
        (work/'wire_data.h').write_text('\n'.join(
            'static const unsigned char wire_'+name+'[] = {' + ','.join(map(str, value)) + '};'
            for name, value in data))
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc')
        exe = work/'view'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-D__KERNEL_BUILD__',
            '-I'+str(work), '-I'+str(work/'gfx'), '-I'+str(work/'drivers/video/pc98'),
            *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/pc98'),
            str(work/'tools/tests/osri_view_host.c'), '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode:
            print('runtime rc=', result.returncode, result.stderr.decode(), flush=True)
        return result.returncode

def no_division():
    with tempfile.TemporaryDirectory(prefix='osri-symbols-') as td:
        cc = Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'
        for rel in ['drivers/video/pc98/splash_raster_run.c', 'drivers/video/pc98/backend_pc98.c', 'drivers/video/pc98/backend_pc98_osri.c']:
            obj = Path(td)/'check.o'
            subprocess.run([str(cc), '-std=gnu11', '-O2', '-ffreestanding', '-D__KERNEL_BUILD__',
                *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/pc98'),
                '-c', str(ROOT/rel), '-o', str(obj)], check=True)
            symbols = subprocess.check_output([str(cc.with_name('i386-elf-nm')), '-u', str(obj)], text=True)
            assert not re.search(r'__(?:u?div|u?mod)di3', symbols), symbols
    print('GREEN run/backend have no 64-bit division helpers', flush=True)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--mutate', action='store_true'); args=p.parse_args()
    assert execute() == 0
    print('GREEN A1/A2/A4/A5 entry, faults, error classification, ownership, cleanup, deadlines, input and IF boundary', flush=True)
    no_division()
    if args.mutate:
        for mutation in MUTATIONS:
            assert execute(mutation) != 0, 'survived: '+mutation[0]
            print('RED '+mutation[0]+': compiled, runtime failure in '+mutation[4], flush=True)
        assert execute() == 0
        print('GREEN restored OSRI viewer', flush=True)
if __name__ == '__main__': main()
