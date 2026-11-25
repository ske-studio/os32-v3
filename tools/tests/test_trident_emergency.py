#!/usr/bin/env python3
"""Trident 3c: resident recovery, real ISR/exit prefixes and gfx core wiring.

The prefixes stop after the first TVRAM output (before register/stack dumps).
The classifier, hook, rc handling and actual TVRAM calls remain product code.
Hardware sequence assembly belongs to test_trident_lifecycle, in tri-life.
"""
CHECK = dict(name='trident-emergency', tier=1, needs=['make:all'], budget=15.1,
    covers=['tools/tests/gfx2_core_fake.h', 'tools/tests/gfx2_bridge_fake.h', 'sys/include/copy_units.h', 'arch/*/arch_copy.h', 'sys/hal/gfx/sync/sync_int.h', 'sys/hal/gfx/v2/gfx2_int.h', 'sys/include/gfx_sync.h', 'sys/hal/gfx/gfx_hal2.h', 'sdk/include/os32/os32_gfx2.h', 'tools/tests/recover_gf_dev_stub.c', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/include/legacy_guest.h', 'tools/tests/legacy_guest_fake.h', 'sys/hal/gfx/gfx_emergency.c', 'sys/hal/gfx/gfx_emergency.h', 'sys/hal/gfx/gfx_core.c',
            'gfx/gfx_core*.inc', 'sys/dev/video/cirrus/backend_cirrus.c',
            'sys/platform/pc98/video/cirrus/cirrus_identify.c', 'sys/platform/pc98/video/backend_pc98.c',
            'sys/platform/pc98/video/backend_pegc.c', 'sys/kern/exec/access_walk.c', 'sys/kern/exec/lease.c',
            'sys/kern/exec/redir_access.c', 'sys/kern/exec/surface_query.c', 'sys/hal/gfx/gfx_scroll.c', 'sys/platform/pc98/gfx_scroll.c', 'sys/platform/pc98/gfx_init.c', 'sys/platform/pc98/gfx_selector.c', 'sys/platform/pc98/gfx_selector_lifecycle.inc', 'sys/platform/pc98/gfx_surfaces.c', 'sys/kern/console/display_restore.h',
            'sys/platform/pc98/video/gfx_vram.c', 'sys/hal/gfx/gfx_dirty.c', 'sys/hal/gfx/gfx_present.c', 'sys/kern/sys.c', 'sys/kern/mm/paging.c', 'sys/kern/mm/pgalloc.c',
            'sys/kern/mm/physmem.c', 'sys/kern/mm/shlib.c', 'tools/tests/mutpar.py', 'sys/include/gfx_hal.h', 'build/kernel.mk',
            'sys/arch/x86/kernel/isr_handlers.c', 'sys/arch/x86/kernel/idt.h',
            'sys/kern/exec/exec_finish.inc', 'sys/kern/exec/appslot.h',
            'tools/tests/trident_emergency_host.c', 'tools/tests/exec_gfx_exit_order_host.c', 'tools/tests/trident_core_host.c',
            'tools/tests/trident_surface_lifetime_host.c', 'drivers/video/trident/*.c',
            'sys/dev/video/trident/probes.h',
            'sys/dev/pci/pci.h', 'sys/dev/pci/pci_decode.h', 'sys/kern/exec/surface_limits.h',
            'sys/dev/video/trident/trident_module.h', 'sys/kern/kstring.h',
            'sys/hal/gfx/gfx.h', 'sys/hal/gfx/gfx_internal.h', 'sys/hal/gfx/palette.h', 'lib/serial_diag.h',
            'sys/kern/mm/kmalloc.h', 'sys/dev/char/serial.h', 'userland/diag/hw/trident_mode.hwb',
            'tools/tests/trident_device_fake.c','tools/tests/trident_sync_fake.c', 'tools/tests/trident_device_fake.h',
            'sys/dev/video/trident/trident.h', 'tools/tests/test_gfx_kernel_fb.py',
            'tools/tests/gfx_kernel_fb_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'tools/tests/kernel_layout.py',
            'tools/tests/paging_source.py', 'tools/tests/serial_diag_host.h',
            'tools/tests/host_arch/**', 'build/config.mk', 'include/**',
            'sdk/include/os32/**', 'tools/tests/test_trident_emergency.py', 'sys/include/exec_arch.h', 'sys/arch/x86/arch_exec_storage.h', 'tools/tests/host_arch/arch_exec_storage.h', 'tools/tests/host_exec_context.h', 'sys/arch/x86/kernel/exec_context.c', 'sys/arch/x86/kernel/exec_entry.c', 'tools/tests/host_exec_entry.h', 'sys/arch/x86/kernel/tss.h'],
    basis=['TRIDENT_FULL_DESIGN §2', 'REALHW_RECOVER_DESIGN §2', 'REALHW_RECOVER_DESIGN §3', 'REALHW_RECOVER_DESIGN §4'],
)
import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

import host32
from paging_source import paging_source
from kernel_includes import include_flags
from test_gfx_kernel_fb import run_case

ROOT = Path(__file__).resolve().parents[2]
DISPATCH = 'sys/hal/gfx/gfx_emergency.c'
ISR = 'sys/arch/x86/kernel/isr_handlers.c'
EXIT = 'sys/kern/exec/exec_finish.inc'
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-Wno-unused-function', '-Wno-unused-parameter', '-Wno-unused-variable', '-ffreestanding', '-fno-pie',
         '-fno-stack-protector', '-nostdlib', '-static', '-no-pie']

# Each literal product anchor must be unique. RED must come from execution.
MUTATIONS = [
    ('M_SELECT_RETRY', 'sys/platform/pc98/gfx_selector.c',
     'if (want == &gfx_backend_trident && want && want->prepare) want->prepare();',
     'if (0) want->prepare();', 'core'),
    ('live-owner', DISPATCH, '!GFX_EMERGENCY_MATCHES_OWNER(owner, appslot_gfx_owner)',
     '(owner != 2 && owner != 0)', 'entries'),
    ('kernel-owner', DISPATCH,
     '!GFX_EMERGENCY_MATCHES_OWNER(owner, appslot_gfx_owner)',
     'owner != (u32)appslot_gfx_owner()', 'entries'),
    ('recursion', DISPATCH, '!emergency_restore || returning ||',
     '!emergency_restore ||', 'entries'),
    ('drop-rc', DISPATCH, '    return rc;\n}', '    return rc ? 0 : rc;\n}', 'entries'),
    ('disarm-after-return', DISPATCH, '    returning = 0;\n',
     '    returning = 0; emergency_restore = 0;\n', 'entries'),
    ('replace-arm', DISPATCH, 'if (emergency_restore)\n',
     'if (emergency_restore && emergency_opaque != opaque)\n', 'entries'),
    ('general-hook', ISR,
     'gfx_return_emergency(GFX_EMERGENCY_KERNEL_OWNER, (int)vector)', '0', 'entries'),
    ('pf-hook', ISR,
     'gfx_return_emergency(GFX_EMERGENCY_KERNEL_OWNER, (int)error_code)', '0', 'entries'),
    ('skip-tvram-on-error', ISR, '    /* Exception recovery failure must not suppress TVRAM fault output. */',
     '    if (restore_rc) return;\n    /* Exception recovery failure must not suppress TVRAM fault output. */', 'entries'),
    ('skip-pf-tvram-on-error', ISR,
     '    /* #PF recovery failure must not suppress TVRAM fault output. */',
     '    if (restore_rc) return;\n    /* #PF recovery failure must not suppress TVRAM fault output. */', 'entries'),
    ('owner-hook-late', EXIT, '    exec_gfx_owner_exit(id);\n    appslot_gfx_owner_exit(id);',
     '    appslot_gfx_owner_exit(id);\n    exec_gfx_owner_exit(id);', 'entries'),
    ('sfs-owner-exit', EXIT, '    sfs_persistent_scope_owner_exit(id);', '', 'entries'),
    ('owner-hook-missing', EXIT, '    exec_gfx_owner_exit(id);', '', 'entries'),
    ('exit-order', EXIT,
     '    int restore_rc = exec_gfx_return_emergency((u32)id, kind);',
     '    appslot_gfx_owner_exit(id);\n    int restore_rc = exec_gfx_return_emergency((u32)id, kind);', 'entries'),
    ('child-exit', EXIT, 'exec_gfx_return_emergency((u32)id, kind)',
     'exec_gfx_return_emergency(GFX_EMERGENCY_KERNEL_OWNER, kind)', 'entries'),
    ('boot-hook', 'sys/platform/pc98/gfx_surfaces.c',
     'if (pref == GFX_PREF_TRIDENT && trident_boot_reserve)',
     'if (pref == GFX_PREF_AUTO && trident_boot_reserve)', 'core'),
    ('sf5', 'sys/platform/pc98/gfx_selector.c',
     'be == &gfx_backend_trident ? LEDGER_SF_TRIDENT :',
     'be == &gfx_backend_trident ? LEDGER_SF_PC98 :', 'core'),
    ('client-binding', 'sys/hal/gfx/gfx_core_framebuffer.inc',
     'GfxBackend *backend = g_backend;',
     'GfxBackend *backend = DISPLAY_FALLBACK_BACKEND;', 'core'),
    ('auto-probe', 'sys/platform/pc98/gfx_selector.c',
     'g_backend_list[i] == &gfx_backend_trident) continue;',
     'g_backend_list[i] == &gfx_backend_pc98) continue;', 'core'),
    ('drop-init-recovery', 'sys/platform/pc98/gfx_selector.c',
     'int restore_rc = gfx_return_emergency(GFX_EMERGENCY_KERNEL_OWNER, rc);',
     'int restore_rc = rc ? 0 : rc;', 'core'),
    ('unsafe-fallback', 'sys/platform/pc98/gfx_selector.c',
     'if (restore_rc || (pc98_route_safe && !pc98_route_safe())) return -1;',
     'if (restore_rc && pc98_route_safe && pc98_route_safe()) return -1;', 'core'),
]

def function(text, name):
    match = re.search(r'^(?:static )?(?:void|int) ' + name + r'\([^\{]*\)\n\{', text, re.M)
    assert match, name
    pos, depth = match.end(), 1
    while depth:
        depth += (text[pos] == '{') - (text[pos] == '}')
        pos += 1
    return text[match.start():pos]


def source(path, mutation):
    text = paging_source(ROOT / path)
    if mutation and mutation[1] == path:
        _, _, old, new, _ = mutation
        assert text.count(old) == 1, (mutation[0], text.count(old))
        text = text.replace(old, new)
    return text


def entries(work, runner, mutation):
    isr = source(ISR, mutation)
    general = function(isr, 'exception_handler')
    general = general[:general.index('    if (vector < EXCEPTION_NAME_COUNT)')] + '}\n'
    pf = function(isr, 'page_fault_handler')
    pf = pf[:pf.index('    sputs("\\n\\n==== PAGE FAULT')] + '}\n'
    finish = function(source(EXIT, mutation), 'exec_notify_owned')
    (work / 'trident_emergency_entries.inc').write_text(general + pf + finish)
    product = work / 'gfx_emergency.c'
    product.write_text(source(DISPATCH, mutation).replace('"../exec/appslot.h"', '"' + str(ROOT / 'sys/kern/exec/appslot.h') + '"'))
    exe = work / 'emergency'
    subprocess.run(['gcc', *FLAGS, '-I' + str(work), '-I' + str(ROOT),
                    '-I' + str(ROOT / 'tools/tests'), '-I' + str(ROOT / 'gfx'),
                    *include_flags(ROOT), str(product),
                    str(ROOT / 'tools/tests/trident_emergency_host.c'),
                    str(ROOT / 'tools/tests/trident_device_fake.c'), '-o', str(exe)], check=True)
    return host32.run([str(exe)], runner=runner, capture_output=True, text=True)


def core(runner, mutation):
    changes = []
    if mutation:
        _, path, old, new, _ = mutation
        # run_case expands the production .inc files into gfx_core.c.
        changes = [('sys/hal/gfx/gfx_core.c' if path.endswith('.inc') else path, old, new)]
    def stage(tmp, sources):
        sources.append(ROOT / 'tools/tests/trident_device_fake.c')
        sources.append(ROOT / 'tools/tests/trident_sync_fake.c')
    fixture = 'unsigned int host_arch_if = 0x202U;\n#include "trident_core_host.c"\n'
    return run_case(None, runner, changes, fixture, stage=stage)


def lifetime(runner, mutation=None, fallback=1):
    def stage(tmp, sources):
        # Real boot reserves two additional master PTs for BAR0/BAR1.
        host = (ROOT / 'tools/tests/gfx_kernel_fb_host.c').read_text()
        assert host.count('u32 pages = 1;') == 1
        (tmp / 'lifetime_kernel_host.c').write_text(host.replace('u32 pages = 1;', 'u32 pages = 3;'))
        directory = ROOT / 'drivers/video/trident'
        for stem in ('resources', 'lifecycle', 'regs', 'probe', 'state_save',
                     'pc98_init', 'mode_640', 'route_ra266', 'vram', 'default_dac',
                     'state_restore', 'present',  'palette', 'screen', 'video',
                     'dpms', 'accel', 'cursor', 'backend_trident'):
            body = source('drivers/video/trident/' + stem + '.c', mutation)
            if stem == 'resources':
                body = f'#define TRI_RAM_FALLBACK_DEFAULT {fallback}\n' + body
            for header in ('trident.h', 'trident_module.h', 'probes.h'):
                body = body.replace('"' + header + '"', '"' + str(directory / header) + '"')
            if stem == 'backend_trident':
                body = body.replace('trident_regs_bind(&device)', 'lifetime_bind(&device)')
                body = '#define trident_backend_ops gfx_backend_trident\n' + body
                (tmp / 'lifetime_backend.c').write_text(body)
            else:
                path = tmp / ('lifetime_' + stem + '.c')
                path.write_text(body)
                sources.append(path)
        sources.append(ROOT / 'tools/tests/trident_device_fake.c')
        sources.append(ROOT / 'tools/tests/trident_sync_fake.c')
    fixture = f'#define TRI_RAM_FALLBACK_DEFAULT {fallback}\n' + 'unsigned int host_arch_if = 0x202U;\n#include "trident_surface_lifetime_host.c"\n'
    return run_case(None, runner, (), fixture, stage=stage)


LIFETIME_MUTATIONS = [
    ('prepare-configure', 'sys/dev/video/trident/backend_trident.c',
     'rc = r->configure(&provisional, bytes, false);', 'rc = 0;', 'lifetime'),
    ('final-configure', 'sys/dev/video/trident/lifecycle.c',
     'configure(&d->mode.selected, d->vram.first_off, true)',
     'configure(&d->mode.selected, d->vram.first_off, false)', 'lifetime'),
    ('close-default', 'sys/dev/video/trident/backend_trident.c',
     'if (device.mode.valid) device.mode.selected = provisional;',
     'if (false) device.mode.selected = provisional;', 'lifetime'),
    ('close-default-pitch', 'sys/dev/video/trident/backend_trident.c',
     'provisional.display_width = modes[i].pitch / (provisional.bpp / BITS_PER_BYTE);',
     'provisional.display_width = TRI_PITCH;', 'lifetime'),
]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
    assert runners
    with tempfile.TemporaryDirectory(prefix='trident-emergency-') as td:
        work = Path(td)
        for runner in runners:
            result = lifetime(runner, fallback=0)
            assert result.returncode == 0 and 'GREEN' in result.stdout, result
            print('GREEN lifetime default=0', flush=True)
            for kind in ('entries', 'core', 'lifetime'):
                result = entries(work, runner, None) if kind == 'entries' else core(runner, None) if kind == 'core' else lifetime(runner)
                print(result.stdout + result.stderr, end='', flush=True)
                assert result.returncode == 0 and 'GREEN' in result.stdout, result
        if args.mutate:
            for m in MUTATIONS + LIFETIME_MUTATIONS:
                # Check uniqueness in the actual source, even for expanded inc.
                source(m[1], m)
                result = entries(work, runners[0], m) if m[4] == 'entries' else core(runners[0], m) if m[4] == 'core' else lifetime(runners[0], m, fallback=0 if m[0] == 'close-default-pitch' else 1)
                assert result.returncode == 1 and ('RED ' in result.stdout or 'FAIL ' in result.stdout), (m[0], result)
                print('MUTATE RED ' + m[0], flush=True)
            for fallback in (0, 1):
                result = lifetime(runners[0], fallback=fallback)
                assert result.returncode == 0 and 'GREEN' in result.stdout, result
                print(f'RESTORED default={fallback} ' + result.stdout.strip(), flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_emergency.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
