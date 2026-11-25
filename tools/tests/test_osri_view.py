"""OSRI KAPI: real entry, loader, owner, HAL, deadline and VBLANK poll."""
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile
import struct
import zlib
import host32
from kernel_includes import include_flags
from test_splash_wire import osri

ROOT = Path(__file__).resolve().parents[2]
SOURCES = ['tools/tests/gfx2_bridge_fake.h', 'tools/tests/osri_view_host.c', 'tools/tests/splash_raster_host.c',
           'sys/platform/pc98/platform_osri.h', 'sys/kern/gfx/osri/gfx_osri_view_internal.h', 'sys/kern/gfx/osri/gfx_osri.c', 'sys/kern/gfx/osri/gfx_osri_view.c', 'sys/kern/gfx/osri/gfx_osri_view.h', 'sys/hal/gfx/gfx_internal.h',
           'sys/kern/exec/appslot.h', 'sys/kern/exec/exec.h',
           'sys/kapi/kapi_osri.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pc98_osri.c',
           'sys/platform/pc98/video/splash_raster.c', 'sys/platform/pc98/video/splash_raster_play.c',
           'sys/platform/pc98/video/splash_raster_plan.c', 'sys/platform/pc98/video/splash_raster_run.c',
           'sys/platform/pc98/video/splash_raster_run.h', 'sys/platform/pc98/video/splash_raster_play.h',
           'lib/le_internal.h', 'lib/crc32_core.inc']
# Each tuple's last member names the runtime assertion group it must kill.
MUTATIONS = (
    ('skip first expired present', 'sys/platform/pc98/video/backend_pc98_osri.c',
     '    struct PlV2Plan *plan = 0;',
     '    if (image->count && (unsigned long long)(tick_count - ctl->start) * PL_RUN_TICK_US >= '
     '(unsigned long long)ctl->duration_ms * 1000) return OS32_ERR_AGAIN;\n    struct PlV2Plan *plan = 0;',
     'deadlines'),
    ('PCM running only', 'sys/platform/pc98/platform_osri.h', 'pcm_state() != PCM_ST_CLOSED',
     'pcm_state() == PCM_ST_RUNNING', 'entry_and_failures'),
    ('serial gate omitted', 'sys/kern/gfx/osri/gfx_osri_view_internal.h', 'serial_gate_active() ||',
     '(0 && serial_gate_active()) ||', 'entry_and_failures'),
    ('inverted time limit', 'sys/kapi/kapi_osri.c', 'duration_ms > GFX_OSRI_SHOW_MAX_MS',
     'duration_ms <= GFX_OSRI_SHOW_MAX_MS', 'entry_and_failures'),
    ('omit copy', 'sys/kapi/kapi_osri.c',
     '!copy_caller_bytes(&caller, path + i, copy + i, 1)',
     '(copy[i] = path[i], 0)', 'entry_and_failures'),
    ('omit rebase', 'sys/platform/pc98/video/splash_raster_run.c',
     'r->start = now;', '(void)now;', 'deadlines'),
    ('stack extensions', 'sys/platform/pc98/video/splash_raster_run.c',
     'remaining > extension_us ? remaining : extension_us',
     'remaining + extension_us', 'deadlines'),
    ('double consume', 'sys/platform/pc98/video/backend_pc98_osri.c',
     'int key = input_trygetkey();',
     'int key = input_trygetkey(); (void)input_trygetkey();', 'deadlines'),
    ('ignore abort', 'sys/platform/pc98/video/backend_pc98_osri.c',
     'if (gfx_osri_poll_abort(p->caller)) return 1;',
     'if (0 && gfx_osri_poll_abort(p->caller)) return 1;', 'deadlines'),
    ('ignore Esc', 'sys/platform/pc98/video/backend_pc98_osri.c',
     'if ((key & 0xff) == OSRI_ESC_ASCII) return 1;',
     'if (0 && (key & 0xff) == OSRI_ESC_ASCII) return 1;', 'deadlines'),
    ('GUI inject only', 'sys/platform/pc98/video/backend_pc98_osri.c',
     'int gui = con_sink_is_enabled();', 'int gui = 0;', 'gui_input'),
    ('ignore stop pending', 'sys/kern/gfx/osri/gfx_osri_view_internal.h',
     '|| appslot_stop_pending()', '|| (0 && appslot_stop_pending())', 'gui_input'),
    ('omit owner', 'sys/kern/gfx/osri/gfx_osri_view_internal.h',
     '(con_sink_is_enabled() && (caller) != appslot_gfx_owner())',
     '(0 && con_sink_is_enabled() && (caller) != appslot_gfx_owner())', 'entry_and_failures'),
    ('omit route', 'sys/platform/pc98/video/backend_pc98_osri.c',
     '(image->count && pl_run_timing(state))',
     '(0 && image->count && pl_run_timing(state))', 'entry_and_failures'),
    ('late poll', 'sys/platform/pc98/video/splash_raster_play.c',
     'if (poll && poll(ctx)) return PL_FRAME_CANCEL;\n    unsigned int flags = irq_save();',
     'unsigned int flags = irq_save();\n    if (poll && poll(ctx)) { irq_restore(flags); return PL_FRAME_CANCEL; }',
     'boundaries_and_fixed'),
    ('ignore poll cancel', 'sys/platform/pc98/video/splash_raster_play.c',
     'if (poll && poll(ctx)) return PL_FRAME_CANCEL;',
     'if (poll && poll(ctx)) { /* continue visible scan */ }', 'boundaries_and_fixed'),
)

def wire_inputs(work):
    """Same v2 row, 31 OUTs with one component per color or 32 with the last green."""
    budget = osri(2)
    within = bytearray(budget)
    struct.pack_into('<H', within, len(within) - 4, 1)
    struct.pack_into('<I', within, 48, 0)
    struct.pack_into('<I', within, 48, zlib.crc32(within))
    bad = bytearray(osri(True))
    bad[96] ^= 1
    data = [('fixed', osri()), ('events', osri(True)), ('budget', budget),
            ('within', within), ('bad_crc', bad), ('trailing', osri(True) + b'X')]
    (work / 'wire_data.h').write_text('\n'.join(
        'static const unsigned char wire_' + name + '[] = {' + ','.join(map(str, value)) + '};'
        for name, value in data))


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
        wire_inputs(work)
        cc = str(Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc')
        exe = work/'view'
        subprocess.run([cc, '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
            '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-D__KERNEL_BUILD__',
            '-I'+str(work), '-I'+str(work/'gfx'), '-I'+str(work/'drivers/video/pc98'),
            '-I'+str(work/'platform/pc98'), *include_flags(ROOT), '-I'+str(ROOT/'drivers/video/pc98'),
            str(work/'tools/tests/osri_view_host.c'), '-lgcc', '-o', str(exe)], check=True)
        result = host32.run([str(exe)], runner='qemu', capture_output=True)
        if result.returncode:
            print('runtime rc=', result.returncode, result.stderr.decode(), flush=True)
        return result.returncode

def powermac_view(mutate):
    """DESIGN.final §3: bind before VFS, private planes, user shell input."""
    view = ROOT / 'sys/platform/powermac/osri_view.c'
    platform = (ROOT / 'sys/platform/powermac/platform_osri.h').read_text()
    variants = [('user shell', view.read_text(), platform, 0)]
    if mutate:
        anchor = 'if (generation != of_fb_osri_generation())'
        assert view.read_text().count(anchor) == 1
        variants.append(('ignore load rebind', view.read_text().replace(anchor, 'if (!generation)'), platform, 1))
        anchor = '    osri_active = 0;'
        assert view.read_text().count(anchor) == 1
        variants.append(('clear borrowed packed CLIENT', view.read_text().replace(
            anchor, '    client.planes[0][0] = 0;\n' + anchor), platform, 1))
        anchor = 'platform_osri_pcm_busy(void) { return 0; }'
        assert platform.count(anchor) == 1
        variants.append(('require audio provider', view.read_text(),
                         platform.replace(anchor, anchor.replace('return 0', 'return 1')), 1))
    with tempfile.TemporaryDirectory() as td:
        work = Path(td)
        wire_inputs(work)
        cc = Path(os.environ['CROSS_DIR']) / 'bin/i386-elf-gcc'
        for name, source, header, expected in variants:
            (work / 'platform_osri.h').write_text(header)
            tmp_view = work / 'view.c'
            tmp_view.write_text(source)
            exe = work / 'test'
            subprocess.run([str(cc), '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
                '-ffreestanding', '-fno-pie', '-fno-stack-protector', '-nostdlib', '-nostdinc',
                '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections', '-DKBOOT_LINK=1',
                '-D__KERNEL_BUILD__', '-DKBOOT_SH=1', '-I' + str(work),
                '-I' + str(ROOT / 'platform/powermac'), '-I' + str(ROOT), *include_flags(ROOT), '-I' + str(ROOT / 'arch/ppc'), str(tmp_view),
                str(ROOT / 'sys/kern/serial_diag.c'), str(ROOT / 'sys/platform/powermac/console_stream.c'),
                str(ROOT / 'sys/kern/gfx/osri/gfx_osri.c'), str(ROOT / 'tools/tests/powermac_osri_host.c'),
                '-lgcc', '-o', str(exe)], check=True)
            assert host32.run([str(exe)], runner='qemu').returncode == expected, name
            print(('RED ' if expected else 'GREEN ') + 'PowerMac view ' + name, flush=True)

def no_division():
    with tempfile.TemporaryDirectory(prefix='osri-symbols-') as td:
        cc = Path(os.environ['CROSS_DIR'])/'bin/i386-elf-gcc'
        for rel in ['sys/platform/pc98/video/splash_raster_run.c', 'sys/platform/pc98/video/backend_pc98.c', 'sys/platform/pc98/video/backend_pc98_osri.c']:
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
    powermac_view(args.mutate)
    no_division()
    if args.mutate:
        for mutation in MUTATIONS:
            assert execute(mutation) != 0, 'survived: '+mutation[0]
            print('RED '+mutation[0]+': compiled, runtime failure in '+mutation[4], flush=True)
        assert execute() == 0
        print('GREEN restored OSRI viewer', flush=True)
if __name__ == '__main__': main()
