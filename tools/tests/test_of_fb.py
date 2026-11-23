#!/usr/bin/env python3
"""Characterization of pre-optimization OF scanout; ppc-offb-fast PACK (2026-11-15).

PPC_KAPI_GFX_REST_DESIGN 5c §2-5, A3 and A8 specify transfer accounting and fixed mode.
Keep edge-word bytes, clipping and protected rows while accelerating aligned rows.
"""
import argparse
import os
import re
from pathlib import Path
import subprocess
import tempfile
import host32
from kernel_includes import include_flags
from mutpar import build_in_tree

ROOT = Path(__file__).resolve().parents[2]
BACKEND = 'drivers/video/of_fb/backend_of_fb.c'
V2 = 'drivers/video/of_fb/of_fb_v2.c'
SOURCES = [BACKEND, V2, 'platform/powermac/display_provider.c',
           'platform/powermac/display_dual.c', 'platform/powermac/display.c', 'gfx/gfx_scroll.c']
CHECK = dict(env=['ppc-cross'], name='of-fb-host', tier=1, needs=[], budget=30, covers=[
    'drivers/video/of_fb/backend_of_fb.c', 'drivers/video/of_fb/of_fb_v2.c',
    'drivers/video/of_fb/of_fb_osri.inc', 'gfx/gfx_osri.h', 'gfx/gfx_osri_view.h',
    'gfx/osri_profile.h', 'kernel/idle_work.h', 'include/kernel_tick.h', 'mm/kmalloc.h',
    'platform/powermac/display_provider.c', 'platform/powermac/display_dual.c',
    'gfx/gfx_scroll.c', 'platform/powermac/display.c', 'drivers/video/of_fb/backend_of_fb.h',
    'platform/powermac/platform_display_fallback.h', 'tools/tests/of_fb_host.c',
    'tools/tests/test_of_fb.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'gfx/palette.c', 'gfx/palette.h', 'gfx/gfx_core_api.inc', 'gfx/v2/palette_state.c', 'gfx/v2/caps.c', 'gfx/v2/enum.c', 'gfx/v2/backend_reg.c', 'gfx/v2/gfx2_int.h',
    'platform/powermac/display_text.h', 'exec/appslot.h', 'include/gfx_present_contract.h',
    'include/gfx_hal.h', 'include/gfx_hal2.h', 'include/gfx_mode.h',
    'include/display_provider.h', 'platform/powermac/rh_observe.h',
    'arch/ppc/kernel_boot.h', 'kernel/console/console_dual.h'],
    basis=['PPC_FBCON_DESIGN §2', 'AC-DISPLAY-2', 'PPC_KAPI_GFX_REST_DESIGN 5c §2-5 A3 A8'],
)
OSRI = 'drivers/video/of_fb/of_fb_osri.inc'
MUTATIONS = (
    ('extension-disabled', OSRI, 'if (deadline < extension) deadline = extension;',
     'if (deadline < extension) deadline = elapsed;'),
    ('physical-index-remapped', OSRI, 'dst[x] = *pixels++;',
     '{ u32 word = *pixels++; u8 *bytes = (u8 *)&word; for (int j = 0; j < 4; j++) bytes[j] = scan_index[bytes[j]]; dst[x] = word; }'),
    ('plane-bit', OSRI, '128u >> (x % 8)', '1u << (x % 8)'),
    ('late-event', OSRI, 'event[event].line == y', 'event[event].line + 1u == y'),
    ('generation-lost', OSRI, 'if (of_fb_osri_generation() != bound)', 'if (!of_fb_osri_generation() && bound)'),
    ('fixed-deadline', OSRI, 'if (image->count && elapsed >= deadline) return GFX_OSRI_SHOW_DONE;',
     'if (elapsed >= deadline) return GFX_OSRI_SHOW_DONE;'),
    ('public-color-rejected', 'gfx/gfx_core_api.inc',
     'if (DISPLAY_SET_PALETTE(g_backend, idx, 1, rgb)) return;',
     '(void)DISPLAY_SET_PALETTE(g_backend, idx, 1, rgb);'),
    ('lease-rejected', 'gfx/gfx_core_api.inc',
     'if (rc) return rc;\n    gfx2_pal_set_v1(first', '(void)rc;\n    gfx2_pal_set_v1(first'),
    ('palette-owner', BACKEND, 'if (!fb.palette_allowed || !fb.palette_allowed()) return OS32_ERR_BUSY;', ''),
    ('clut-index', BACKEND, 'scan_index[i] = best;', 'scan_index[i] = i;'),
    ('count-commit', BACKEND, 'if (!rc) gfx_counters.commits++;', ''),
    ('count-bytes', BACKEND, 'gfx_counters.present_bytes += (end - first)', 'gfx_counters.present_bytes += 1u'),
    ('fixed-mode', V2, 'm->id != 1 ||', ''),
    ('present-owner', SOURCES[2], 'present_owner == appslot_present_owner()', '1'),
    ('handler-present-owner', SOURCES[2], 'present_owner == appslot_present_owner()',
     'present_owner == appslot_cur()'),
    ('present-live', SOURCES[2], 'a->state == GFX2_AD_OK &&', ''),
    ('present-generation', SOURCES[2], 'present_generation == a->gen', '1'),
    ('present-reset', SOURCES[2], '{ present_owner = 0; }', '{ }'),
    ('text-ready', 'platform/powermac/display_dual.c',
     'pm_console_text_ready() && of_fb_ready()', 'of_fb_ready()'),
    ('text-generation', 'platform/powermac/display_dual.c',
     'text_gen == a->gen', '1'),
    ('unverified-colors', 'platform/powermac/display_provider.c',
     '(pm_rh_full_colors()->rc != PM_RH_COLOR_OK &&\n         pm_rh_full_colors()->rc != PM_RH_COLOR_WRITTEN) ||', ''),
    ('pref', 'platform/powermac/display_provider.c',
     'int display_provider_pref(int pref) { (void)pref; return GFX_PREF_AUTO; }',
     'int display_provider_pref(int pref) { return pref; }'),
    ('absent-boot-bind', 'platform/powermac/display_provider.c',
     '    if (saved.display || !fb || !dev || dev->fb_full_rc != PM_RH_FULL_OK ||',
     '    bind_client();\n    if (saved.display || !fb || !dev || dev->fb_full_rc != PM_RH_FULL_OK ||'),
    ('display-records', 'platform/powermac/display_provider.c',
     'u32 display_provider_display_records(u32 backend) { (void)backend; return 1; }',
     'u32 display_provider_display_records(u32 backend) { return backend + 1; }'),
    ('fallback-mode', 'platform/powermac/display_provider.c',
     '    fn(&gfx_backend_unsupported, legacy200);', '    (void)0;'),
    ('pitch', BACKEND, 'dest_row * fb.pitch + first', 'dest_row * (src ? pitch : fb.width) + first'),
    ('aligned-last-word', BACKEND, 'i < words; i++) dest[i] = aligned[i]',
     'i + 1 < words; i++) dest[i] = aligned[i]'),
    ('surface-connect', V2, 'return surface_copy(c, head, s);\n}\nstatic int shim_flip', '(void)head; (void)s; return 0;\n}\nstatic int shim_flip'),
    ('bound', BACKEND, 'if (right > fb.width) right = fb.width;', 'if (right > fb.width) right = fb.width + 4;'),
    ('protected-band', BACKEND, 'if ((u32)dest_row < fb.protected_rows) continue;', 'if (row < fb.protected_rows) continue;'),
    ('packed-enter', SOURCES[2], 'if (of_fb_ready() && gfx_is_started()) of_fb_reset_frame();', ''),
    ('clut-origin', BACKEND, 'if (changed) {', 'if (changed) { origin = 0;'),
    ('scroll-sign', BACKEND, '(int)row - origin', '((int)row + origin) % (int)fb.height'),
    ('scroll-shadow', BACKEND, 'origin = y;\n    transfer(0, 0,',
     'origin = y;\n    transfer(fb.shadow, fb.shadow_pitch,'),
    ('origin-sync', BACKEND, 'gfx_scroll_reset_origin();', ''),
    ('lost', V2, '!a || a->state != GFX2_AD_OK || !of_fb_ready()', '!a || !of_fb_ready()'),
)


def observe(folder, mutation=None):
    edits = {}
    if mutation:
        name, path, old, new = mutation
        source = (ROOT / path).read_text()
        assert source.count(old) == 1, name
        edits[path] = source.replace(old, new, 1)
    exe = folder / 'of-fb'
    command = ['gcc', '-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra', '-Werror',
               '-ffreestanding', '-fno-builtin', '-fno-pie', '-no-pie', '-fno-stack-protector',
               '-nostdlib', '-nostdinc', '-static', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-I' + str(ROOT / 'platform/powermac'), '-I' + str(ROOT), *include_flags(ROOT),
               '-I' + str(ROOT / 'arch/ppc'),
               str(ROOT / 'tools/tests/of_fb_host.c'),
               *(str(ROOT / path) for path in SOURCES),
               str(ROOT / 'gfx/palette.c'), str(ROOT / 'gfx/v2/palette_state.c'), str(ROOT / 'gfx/v2/backend_reg.c'), str(ROOT / 'gfx/v2/caps.c'), str(ROOT / 'gfx/v2/enum.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), (mutation, result)
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print('RED runtime ' + mutation[0] if mutation else 'GREEN OF framebuffer/provider', flush=True)


def cross(folder):
    cc = Path(os.environ.get('PPC_CROSS_DIR', '/home/hight/opt/cross-ppc')) / 'bin/powerpc-elf-gcc'
    if not cc.is_file():
        print('SKIP PPC cross compiler unavailable', flush=True)
        return
    includes = ['.', 'include', 'arch/ppc', 'platform/powermac', 'sdk/include',
                'sdk/include/os32', 'kernel', 'init', 'kernel/console', 'kernel/irq',
                'kernel/time', 'mm', 'lib']
    for profile in ('default', 'shell', 'sh'):
        for path in SOURCES:
            subprocess.run([str(cc), '-std=gnu11', '-m32', '-mbig-endian', '-mcpu=7450', '-mno-altivec', '-msdata=none',
                '-msoft-float', '-fsigned-char', '-fno-short-enums', '-ffreestanding', '-fno-builtin', '-fno-pie', '-fno-stack-protector',
                '-O2', '-Wall', '-Wextra', '-Werror', '-Werror=vla', '-D__cdecl=',
                '-D__KERNEL_BUILD__', '-DKBOOT_SHELL=' + str(int(profile == 'shell')),
                '-DKBOOT_SH=' + str(int(profile == 'sh')),
                *('-I' + str(ROOT / p) for p in includes), '-c', str(ROOT / path),
                '-o', str(folder / (profile + '-' + Path(path).stem + '.o'))], check=True)
    disasm = subprocess.check_output([str(cc.with_name('powerpc-elf-objdump')), '-d',
        str(folder / 'sh-backend_of_fb.o')], text=True)
    copy = disasm.split('<transfer>:', 1)[1].split('\n\n', 1)[0]
    assert not re.search(r'\b(?:stb|stbx|stbu|sth|sthx|sthu)\b', copy), copy
    assert re.search(r'\bstw(?:x)?\b', copy) and re.search(r'\b(?:hwsync|sync)\b', copy), copy
    print('GREEN PPC -c default/shell/sh (6 units each), word stores + sync', flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    for mutation in (None, *MUTATIONS, None) if args.mutate else (None,):
        with tempfile.TemporaryDirectory(prefix='of-fb-') as directory:
            observe(Path(directory), mutation)
    with tempfile.TemporaryDirectory(prefix='of-fb-cross-') as directory:
        cross(Path(directory))


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', __file__, *(['--mutate'] if ctx.mutate else [])], env=env)


if __name__ == '__main__':
    main()
