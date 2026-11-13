#!/usr/bin/env python3
"""R3 OF framebuffer: INDEX8 scanout/provider and native-word PPC compilation."""
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
           'platform/powermac/display_dual.c', 'platform/powermac/display.c']
CHECK = dict(name='of-fb-host', tier=1, needs=[], budget=30, covers=[
    'drivers/video/of_fb/backend_of_fb.c', 'drivers/video/of_fb/of_fb_v2.c',
    'platform/powermac/display_provider.c', 'platform/powermac/display_dual.c',
    'platform/powermac/display.c', 'drivers/video/of_fb/backend_of_fb.h',
    'platform/powermac/display_fallback.h', 'tools/tests/of_fb_host.c',
    'tools/tests/test_of_fb.py', 'tools/tests/host32.py',
    'tools/tests/kernel_includes.py', 'tools/tests/mutpar.py',
    'gfx/v2/caps.c', 'gfx/v2/enum.c', 'gfx/v2/backend_reg.c', 'gfx/v2/gfx2_int.h',
    'platform/powermac/display_text.h',
    'include/gfx_hal.h', 'include/gfx_hal2.h', 'include/gfx_mode.h',
    'include/display_provider.h', 'platform/powermac/rh_observe.h',
    'arch/ppc/kernel_boot.h', 'kernel/console/console_dual.h'])
MUTATIONS = (
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
    ('pitch', BACKEND, 'row * fb.pitch + col', 'row * fb.width + col'),
    ('surface-connect', V2, 'return surface_copy(c, head, s);\n}\nstatic int shim_flip', '(void)head; (void)s; return 0;\n}\nstatic int shim_flip'),
    ('bound', BACKEND, 'if (right > fb.width) right = fb.width;', 'if (right > fb.width) right = fb.width + 4;'),
    ('protected-band', BACKEND, 'if (y < (int)fb.protected_rows) y = fb.protected_rows;', 'if (y < 0) y = 0;'),
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
               '-nostdlib', '-static', '-ffunction-sections', '-fdata-sections', '-Wl,--gc-sections',
               '-I' + str(ROOT / 'platform/powermac'), '-I' + str(ROOT), *include_flags(ROOT),
               str(ROOT / 'tools/tests/of_fb_host.c'),
               *(str(ROOT / path) for path in SOURCES),
               str(ROOT / 'gfx/v2/backend_reg.c'), str(ROOT / 'gfx/v2/caps.c'), str(ROOT / 'gfx/v2/enum.c'), '-o', str(exe)]
    build_in_tree(ROOT, folder, edits, [command])
    result = host32.run([str(exe)], runner='qemu', capture_output=True, text=True)
    assert result.returncode == (1 if mutation else 0), (mutation, result)
    if mutation:
        assert result.stdout.startswith('FAIL '), result
    print('RED runtime ' + mutation[0] if mutation else 'GREEN OF framebuffer/provider', flush=True)


def cross(folder):
    cc = Path('/home/hight/opt/cross-ppc/bin/powerpc-elf-gcc')
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
    copy = disasm.split('<of_fb_copy>:', 1)[1].split('\n\n', 1)[0]
    assert not re.search(r'\b(?:stb|stbx|stbu|sth|sthx|sthu)\b', copy), copy
    assert re.search(r'\bstw(?:x)?\b', copy) and re.search(r'\b(?:hwsync|sync)\b', copy), copy
    print('GREEN PPC -c default/shell/sh (5 units each), word stores + sync', flush=True)


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
