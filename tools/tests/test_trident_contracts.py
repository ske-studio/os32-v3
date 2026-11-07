#!/usr/bin/env python3
"""3a: standalone ILP32 declarations and a libc-free fake foundation.

Numbers/slots are frozen from TRIDENT_FULL_DESIGN §9 (F0/F16) and the retained 3a prefix;
expected values never come from product output. Complete Init trace oracles
belong to 3b; this suite checks the gate, not the hardware Init sequence.
"""
CHECK = dict(name='trident-contracts', tier=1, needs=[], budget=3,
    covers=['drivers/video/trident/trident.h',
            'drivers/video/trident/trident_module.h', 'gfx/gfx_emergency.h',
            'tools/tests/trident_device_fake.h', 'tools/tests/trident_device_fake.c',
            'tools/tests/trident_contract_host.c', 'tools/tests/host32.py',
            'tools/tests/kernel_includes.py', 'build/config.mk', 'include/types.h',
            'include/memmap.h', 'include/pegc.h', 'gfx/palette.h',
            'mm/pgalloc.h', 'mm/paging.h', 'exec/surface_query.c',
            'sdk/include/os32/os32_surface.h',
            'mm/physmem.h', 'exec/surface_limits.h', 'include/sys.h',
            'include/gfx_hal.h', 'include/gfx_mode.h', 'include/gfx_format.h',
            'gfx/gfx.h', 'sdk/include/os32/os32_kapi_shared.h',
            'sdk/include/os32/os32_generations.h'])
import argparse
import os
from pathlib import Path
import shutil
import subprocess
import tempfile

import host32
from kernel_includes import include_flags

ROOT = Path(__file__).resolve().parents[2]
HEADER = 'drivers/video/trident/trident.h'
MODULE = 'drivers/video/trident/trident_module.h'
EMERGENCY = 'gfx/gfx_emergency.h'
FAKE_H = 'tools/tests/trident_device_fake.h'
FAKE_C = 'tools/tests/trident_device_fake.c'
HOST = 'tools/tests/trident_contract_host.c'
MEMMAP = 'include/memmap.h'
PGALLOC = 'mm/pgalloc.h'
HEADERS = (HEADER, MODULE, EMERGENCY, FAKE_H, MEMMAP, PGALLOC,
           'include/pegc.h', 'gfx/palette.h', 'sdk/include/os32/os32_surface.h')
FLAGS = ['-m32', '-march=i386', '-std=gnu11', '-O2', '-Wall', '-Wextra',
         '-Werror', '-ffreestanding', '-fno-pie', '-fno-stack-protector']
CONSTANTS = {
    'MEM_TRIDENT_LFB_VA': 0x20000000, 'MEM_TRIDENT_REGS_VA': 0x20400000,
    'LEDGER_SF_TRIDENT': 5, 'OS32_SURFACE_TRIDENT': 5,
    'PEGC_PAL_KAPI_BITS': 4, 'GFX_PALETTE_COMPONENT_BITS': 4,
    'GFX_PALETTE_COMPONENT_MASK': 15,
    'TRI_PCI_VENDOR': 0x1023, 'TRI_PCI_DEVICE': 0x9660, 'TRI_CHIP_9682': 0x10,
    'TRI_PCI_BUS': 0, 'TRI_PCI_SLOT': 8, 'TRI_PCI_FUNCTION': 0,
    'TRI_PCI_COMMAND': 2, 'TRI_BAR0_PHYS': 0x20000000, 'TRI_BAR1_PHYS': 0x20400000,
    'TRI_BAR0_DECODE_BYTES': 4194304, 'TRI_BAR1_DECODE_BYTES': 65536,
    'TRI_REG_MAPPED_BYTES': 65536, 'TRI_WIDTH': 640, 'TRI_HEIGHT': 480,
    'TRI_PITCH': 640, 'TRI_DISPLAY_BYTES': 307200, 'TRI_DISPLAY_PAGES': 75,
    'TRI_DAC_COLORS': 256, 'TRI_DAC_BYTES': 768, 'TRI_SAVED_BYTES': 852,
    'TRI_SAVE_MCLK': 0, 'TRIDENT_MODULE_OPS_VERSION': 2,
    'TRIDENT_MODULE_CONTEXT_SIZE': 40, 'TRIDENT_MODULE_OPS_SIZE': 16,
    'TRIDENT_DIAGNOSTIC_OPS_VERSION': 1, 'TRIDENT_DIAGNOSTIC_OPS_SIZE': 104,
    'TRI_MODE_COUNT': 15, 'TRI_FORMAT_COUNT': 5, 'TRI_E_NOSYS': -12,
    'GFX_EMERGENCY_KERNEL_OWNER': 0,
    'TRI_SR_INDEX': 0x3C4, 'TRI_GR_INDEX': 0x3CE,
    'TRI_CR_COLOR': 0x3D4, 'TRI_CR_MONO': 0x3B4,
    'TRI_AR_INDEX': 0x3C0, 'TRI_AR_DATA': 0x3C1,
    'TRI_MISC_READ': 0x3CC, 'TRI_MISC_WRITE': 0x3C2,
    'TRI_DAC_MASK': 0x3C6, 'TRI_DAC_READ_INDEX': 0x3C7,
    'TRI_DAC_WRITE_INDEX': 0x3C8, 'TRI_DAC_DATA': 0x3C9,
    'TRI_MCLK_LO': 0x43C6, 'TRI_MCLK_HI': 0x43C7,
    'TRI_PLL_LO': 0x43C8, 'TRI_PLL_HI': 0x43C9,
    'TRI_EXT_DAC_DATA': 0x83C6, 'TRI_EXT_DAC_INDEX': 0x83C8,
    'TRI_PORT_MODE': 0x68, 'TRI_PORT_FF': 0x6A, 'TRI_PORT_ROUTE': 0xFAC,
    'TRI_SAVE_DAC': 0, 'TRI_SAVE_MISC': 768, 'TRI_SAVE_CR': 769,
    'TRI_SAVE_AR': 794, 'TRI_SAVE_GR': 815, 'TRI_SAVE_SR': 824,
    'TRI_SAVE_SR0E': 828, 'TRI_SAVE_CR13': 829, 'TRI_SAVE_CR21': 830,
    'TRI_SAVE_CR1E': 831, 'TRI_SAVE_CR27': 832, 'TRI_SAVE_CR2B': 833,
    'TRI_SAVE_CR2F': 834, 'TRI_SAVE_CR2A': 835, 'TRI_SAVE_CR3A': 836,
    'TRI_SAVE_CR29': 837, 'TRI_SAVE_CR38': 838, 'TRI_SAVE_CR36': 839,
    'TRI_SAVE_CR39': 840, 'TRI_SAVE_CR55': 841, 'TRI_SAVE_CR50': 842,
    'TRI_SAVE_GR0F': 843, 'TRI_SAVE_GR2F': 844, 'TRI_SAVE_HIDDEN_DAC': 845,
    'TRI_SAVE_CLOCK_MISC': 846, 'TRI_SAVE_PLL_LO': 847, 'TRI_SAVE_PLL_HI': 848,
    'TRI_SAVE_MCLK_LO': 849, 'TRI_SAVE_MCLK_HI': 850, 'TRI_SAVE_SR0D': 851,
    'TRI_OK': 0, 'TRI_E_INVAL': -1, 'TRI_E_DEVICE': -2, 'TRI_E_RESOURCE': -3,
    'TRI_E_MAP': -4, 'TRI_E_SURFACE': -5, 'TRI_E_ACCESS': -6, 'TRI_E_RESTORE': -7,
    'TRI_E_CAP_TOO_SMALL': -8, 'TRI_E_BUSY': -9, 'TRI_E_STATE': -10,
    'TRI_E_MODULE': -11, 'TRI_UPPER_UNKNOWN': 0, 'TRI_UPPER_MIRROR': 1,
    'TRI_UPPER_INDEPENDENT': 2, 'TRI_SPACE_REGS': 0, 'TRI_SPACE_LFB': 1,
    'TRI_FAKE_BLOCKED_WORD': 0xFA7F3F6F, 'TRI_FAKE_REAL_BYTES': 2097152,
    'TRI_FAKE_PORT_SPACE': 2,
}
STEPS = ('NONE', 'RESOURCES', 'PIN', 'ARM', 'IDENTIFY', 'SAVE', 'PC98_INIT',
         'CAPACITY_PLAN', 'MODE', 'ENABLE', None, 'CLIENT', 'ACTIVE',
         'RESTORE', 'LOCK', 'DISABLE', 'CLOSED')
CONSTANTS.update((f'TRI_STEP_{name}', i) for i, name in enumerate(STEPS) if name is not None)
# Fixed ILP32 layout: retained S3 prefix plus F0/F6/F16 additions.
LAYOUTS = {
    'struct trident_saved': (856, dict(value=0, vga_base=852, valid=854)),
    'struct trident_pci_info': (20, dict(vendor=0, device=2, command=4,
        bus=6, slot=7, function=8, revision=9, bar0=12, bar1=16)),
    'struct trident_resources': (132, dict(regs=0, lfb=4, client=8, owner=12,
        bar0_rid=16, bar1_rid=20, decode_bytes=24, mapped_bytes=28,
        display_sid=32, client_sid=36, client_bytes=40, bar0_phys=44,
        bar1_phys=48, regs_bytes=52, pci=56, ready=76, display=80, client_surface=100, generation=120,
        ram_fallback=124, configure=128)),
    'struct trident_device': (2996, dict(res=0, saved=4, declared_bytes=860,
        step=864, last_rc=868, restore_rc=872, disable_rc=876,
        init_done=880, preinit_done=881, active=882, returning=883, armed=884,
        leave_done=885, closed=886, io=888, io_opaque=892, failed_step=896,
        display_owner=900, chip=904, spr=905, reference=906, pinned=907,
        declared_unknown=908, screen_ready=909, cur_vga_base=910, settings=912,
        mode=932, vram=1016, accel=1036, cursor=1092, video=2148,
        dac=2224, overscan=2992, overscan_valid=2993)),
    'struct tri_surface': (20, dict(off=0, bytes=4, pitch=8, width=12,
        height=14, cpp=16, format=17)),
    'struct tri_mode': (32, dict(id=0, dot_khz=4, hd=8, hs=10, he=12,
        ht=14, vd=16, vs=18, ve=20, vt=22, display_width=24, depth=26,
        bpp=27, flags=28)),
    'struct tri_format_cap': (8, dict(format=0, depth=1, bpp=2, cpp=3, features=4)),
    'struct tri_vram_block': (20, dict(next=0, off=4, bytes=8, kind=12, allocated=16)),
    'struct tri_vram_state': (20, dict(blocks=0, usable_bytes=4, pitch=8,
        first_off=12, area2d_end=16)),
    'struct tri_settings': (20, dict(no_accel=0, sw_cursor=1, ram_fallback=2,
        mux_threshold_khz=4, vram_bytes=8, hsync_skew=12, vsync_skew=16)),
    'struct tri_mode_state': (84, dict(selected=0, display=32, client=52,
        generation=72, features=76, valid=80, mux=81, no_accel=82)),
    'struct tri_accel_state': (56, dict(engine_operation=0, draw_flag=4,
        blt_scan_direction=8, pattern_location=12, line_fg=16, line_bg=20,
        line_rop=24, dash_length=25, clipping=26, initialized=27,
        hw_ops=28, io_accesses=32, pattern=36)),
    'struct tri_cursor_state': (1056, dict(allocation=0, image=20, bg=1044,
        fg=1048, available=1052)),
    'struct tri_rect': (16, dict(x1=0, y1=4, x2=8, y2=12)),
    'struct tri_video_image': (44, dict(format=0, width=4, height=8,
        pitch=12, offset=24, bytes=36, data=40)),
    'struct tri_video_state': (76, dict(allocation=0, clip=20, color_key=36,
        status=40, off_time=44, free_time=48, now_ms=52, fix_frame=56,
        hsync=60, vsync=64, last_rc=68, clip_valid=72, polling=73)),
    'struct tri_video_surface': (40, dict(allocation=0, format=20, width=24,
        height=28, pitch=32, on=36)),
    'struct TridentDiagnosticOps': (104, dict(version=0, size=4, context=8,
        set_mode=12, dpms=16, cursor_image=20, cursor_move=24, cursor_show=28,
        sync=32, clear_sync=36, copy_direction=40, line_setup=44, fill=48,
        copy=52, pattern=56, line=60, pattern_upload=64, video_query=68,
        video_put=72, video_set_key=76, video_stop=80, poll=84,
        video_surface_alloc=88, video_surface_display=92,
        video_surface_stop=96, video_surface_free=100)),
    'struct trident_io': (32, dict(read8=0, read16=4, read32=8,
        write8=12, write16=16, write32=20, port8=24, write_span32=28)),
    'TridentModuleContext': (40, dict(backend=0, counters=4, resources=8,
        arm=12, height=16, flip=20, page=24, display_owner=28, size=32, diagnostic_out=36)),
    'struct TridentModuleOps': (16, dict(version=0, initialize=4, prepare=8, restore=12)),
    'struct trident_fake_event': (24, dict(space=0, offset=4, value=8, count=12,
        width=16, write=17, rc=20)),
}
SIGNATURES = {
    'trident_pci_validate': 'int (*)(const struct trident_pci_info *)',
    'trident_boot_reserve': 'int (*)(void)',
    'trident_resources_get': 'const struct trident_resources *(*)(void)',
    'trident_resources_rollback': 'int (*)(bool)',
    'trident_port8': 'int (*)(struct trident_device *, u16, u8)',
    'trident_indexed_read': 'int (*)(struct trident_device *, u16, u8, u8 *)',
    'trident_indexed_write': 'int (*)(struct trident_device *, u16, u8, u8)',
    'trident_ar_read': 'int (*)(struct trident_device *, u16, u8, u8 *)',
    'trident_ar_write': 'int (*)(struct trident_device *, u16, u8, u8)',
    'trident_hidden_dac_read': 'int (*)(struct trident_device *, u8 *)',
    'trident_hidden_dac_write': 'int (*)(struct trident_device *, u8)',
    'trident_ext_dac_update': 'int (*)(struct trident_device *, u8, u8)',
    'trident_protect': 'int (*)(struct trident_device *, u16, bool)',
    'trident_crtc_lock': 'int (*)(struct trident_device *, u16, bool)',
    'trident_present': 'int (*)(struct trident_device *, int, int, int, int)',
    'trident_palette_dac6': 'u8 (*)(u8, u32)',
    'trident_palette': 'int (*)(struct trident_device *, int, int, const u8 *)',
    'trident_restore98': 'int (*)(void *)',
    'gfx_arm_emergency': 'int (*)(int (*)(void *), void *, u32)',
    'gfx_return_emergency': 'int (*)(u32, int)',
    'trident_module_init': 'int (*)(GfxBackend *, const TridentModuleContext *)',
    'trident_backend_prepare': 'int (*)(void)',
    'trident_module_load': 'int (*)(void)', 'trident_module_abort': 'int (*)(void)',
    'trident_fake_reset': 'int (*)(struct trident_fake *, u8 *, u32, struct trident_fake_event *, u32)',
    'trident_fake_bind': 'void (*)(struct trident_fake *, struct trident_device *)',
    'trident_fake_memory': 'int (*)(struct trident_fake *, u32, enum trident_upper)',
    'trident_fake_restored': 'void (*)(struct trident_fake *)',
    'trident_fake_trace_reset': 'void (*)(struct trident_fake *)',
    'trident_fake_fail': 'void (*)(struct trident_fake *, u32, int)',
    'trident_fake_accept_init': 'int (*)(struct trident_fake *, u32, const struct trident_fake_event *, u32)',
}
for name in ('identify', 'regs_bind', 'pc98_init', 'state_save', 'state_restore',
             'capacity_plan', 'video_reset', 'adjust_frame', 'mode_640', 'route_enable',
             'route_disable', 'screen_init', 'enter', 'leave', 'close'):
    SIGNATURES['trident_' + name] = 'int (*)(struct trident_device *)'
for bits in (8, 16, 32):
    SIGNATURES[f'trident_read{bits}'] = f'int (*)(struct trident_device *, enum trident_space, u32, u{bits} *)'
    SIGNATURES[f'trident_write{bits}'] = f'int (*)(struct trident_device *, enum trident_space, u32, u{bits})'
TYPES = {
    'trident_default_dac': 'const u8 [768]',
    'trident_context': 'const TridentModuleContext *',
    'trident_module_ops': 'const struct TridentModuleOps',
    'trident_backend_ops': 'GfxBackend', 'trident_fake_io': 'const struct trident_io',
    '((struct trident_resources *)0)->regs': 'volatile u8 *',
    '((struct trident_resources *)0)->lfb': 'volatile u8 *',
    '((struct trident_resources *)0)->client': 'u8 *',
    '((struct trident_device *)0)->res': 'const struct trident_resources *',
    '((struct trident_device *)0)->io': 'const struct trident_io *',
    '((struct trident_saved *)0)->valid': 'bool',
    '((TridentModuleContext *)0)->display_owner': 'int (*)(void)',
    '((TridentModuleContext *)0)->arm': 'int (*)(int (*)(void *), void *, u32)',
    '((struct TridentModuleOps *)0)->initialize': 'int (*)(GfxBackend *, const TridentModuleContext *)',
    '((struct TridentModuleOps *)0)->prepare': 'int (*)(void)',
    '((struct TridentModuleOps *)0)->restore': 'int (*)(void *)',
}
for bits in (8, 16, 32):
    TYPES[f'((struct trident_io *)0)->read{bits}'] = f'int (*)(void *, enum trident_space, u32, u{bits} *)'
    TYPES[f'((struct trident_io *)0)->write{bits}'] = f'int (*)(void *, enum trident_space, u32, u{bits})'
TYPES['((struct trident_io *)0)->write_span32'] = 'int (*)(void *, u32, const u32 *, u32)'
TYPES['((struct trident_io *)0)->port8'] = 'int (*)(void *, u16, u8)'
# Freeze scalar types too: equal offsets alone do not distinguish int/u32.
for typ, members in {
    'struct trident_resources': ('owner bar0_rid bar1_rid decode_bytes mapped_bytes '
        'display_sid client_sid client_bytes bar0_phys bar1_phys regs_bytes'),
    'struct trident_device': ('declared_bytes step failed_step '
        'display_owner'),
}.items():
    for member in members.split():
        TYPES[f'(({typ} *)0)->{member}'] = 'u32'
for member in ('last_rc', 'restore_rc', 'disable_rc'):
    TYPES[f'((struct trident_device *)0)->{member}'] = 'int'
for member in ('init_done', 'preinit_done', 'active', 'returning', 'armed',
               'leave_done', 'closed', 'pinned', 'declared_unknown'):
    TYPES[f'((struct trident_device *)0)->{member}'] = 'bool'
for member in ('chip', 'spr', 'reference'):
    TYPES[f'((struct trident_device *)0)->{member}'] = 'u8'
for member in ('vendor', 'device', 'command'):
    TYPES[f'((struct trident_pci_info *)0)->{member}'] = 'u16'
for member in ('bus', 'slot', 'function', 'revision'):
    TYPES[f'((struct trident_pci_info *)0)->{member}'] = 'u8'
TYPES.update({
    '((struct trident_resources *)0)->configure': 'int (*)(const struct tri_mode *, u32)',
    '((TridentModuleContext *)0)->diagnostic_out': 'const struct TridentDiagnosticOps **',
    '((struct trident_saved *)0)->vga_base': 'u16',
    '((struct trident_device *)0)->cur_vga_base': 'u16',
    '((struct trident_resources *)0)->ready': 'bool',
    '((struct trident_resources *)0)->pci': 'struct trident_pci_info',
    '((struct trident_device *)0)->saved': 'struct trident_saved',
    '((TridentModuleContext *)0)->backend': 'GfxBackend *',
    '((TridentModuleContext *)0)->counters': 'GfxCounters *',
    '((TridentModuleContext *)0)->resources': 'const struct trident_resources *',
})
for member in ('height', 'flip', 'page'):
    TYPES[f'((TridentModuleContext *)0)->{member}'] = 'int *'
# Each anchor is unique. All mutants must compile, then fail during execution.
MUTATIONS = (
    ('misc-decode', FAKE_C, 'if (inactive_vga(f, off)) return 0xFF;',
     'if (false && inactive_vga(f, off)) return 0xFF;'),
    ('palette-fixed-width', HEADER, 'u32 bits = component_bits;',
     'u32 bits = 4; (void)component_bits;'),
    ('current-owner', EMERGENCY, '(u32)(display_owner)()', '((void)(display_owner)(), 11u)'),
    ('trident-backend-id', PGALLOC, 'LEDGER_SF_TRIDENT    5', 'LEDGER_SF_TRIDENT    4'),
    ('trident-regs-va', MEMMAP, 'MEM_TRIDENT_REGS_VA    0x20400000UL', 'MEM_TRIDENT_REGS_VA    0x20401000UL'),
    ('span-event', FAKE_C, 'f->trace[before].count = count;', 'f->trace[before].count = 1;'),
    ('restore-reblock', FAKE_C, 'if (f->block_after_restore) f->pc98_initialized = false;',
     'if (f->block_after_restore) f->pc98_initialized = true;'),
    ('save-size', HEADER, 'TRI_SAVED_BYTES 852u', 'TRI_SAVED_BYTES 853u'),
    ('last-save-slot', HEADER, 'TRI_SAVE_SR0D = 851', 'TRI_SAVE_SR0D = 850'),
    ('decode-width', HEADER, 'TRI_BAR0_DECODE_BYTES 0x00400000u', 'TRI_BAR0_DECODE_BYTES 0x00200000u'),
    ('module-version', MODULE, 'OPS_VERSION 2u', 'OPS_VERSION 1u'),
    ('resource-owner-order', HEADER, 'owner, bar0_rid, bar1_rid', 'bar0_rid, owner, bar1_rid'),
    ('context-order', MODULE, 'int *height, *flip, *page;', 'int *flip, *height, *page;'),
    ('surface-pitch-order', HEADER, 'u32 off, bytes, pitch;', 'u32 off, pitch, bytes;'),
    ('device-dac-size', HEADER, 'u8 dac[256][3];', 'u8 dac[255][3];'),
    ('diagnostic-order', MODULE, 'u32 version, size;', 'u32 size, version;'),
    ('error-code', HEADER, 'TRI_E_RESTORE = -7', 'TRI_E_RESTORE = -12'),
    ('fake-ready', FAKE_C, 'f->pc98_initialized = false;\n    f->resources', 'f->pc98_initialized = true;\n    f->resources'),
    ('fake-capacity', FAKE_C, 'f->real_bytes = TRI_FAKE_REAL_BYTES;', 'f->real_bytes = TRI_BAR0_DECODE_BYTES;'),
    ('fake-command', FAKE_C, '.command = TRI_PCI_COMMAND,', '.command = 3,'),
    ('fake-gr6', FAKE_C, 'f->gr[6] = 0x0C;', 'f->gr[6] = 5;'),
    ('fake-blocked', FAKE_C, '(u8)(TRI_FAKE_BLOCKED_WORD >>', '(u8)(0xFFFFFFFFu >>'),
    ('fake-linear-only', FAKE_C, 'bool ready = f->pc98_initialized && (f->cr_color[0x21] & 0x20);', 'bool ready = (f->cr_color[0x21] & 0x20);'),
    ('fake-init-oracle', FAKE_C, 'a->width != b->width', 'a->width == 0'),
    ('fake-width', FAKE_C, '.value = v, .width = width,', '.value = v, .width = width == 2 ? 1 : width,'),
    ('fake-failed-write', FAKE_C, 'rc = f->fail_rc;\n    else if ((space', 'rc = 0;\n    else if ((space'),
    ('fake-last-dac', FAKE_C, 'f->dac[f->dac_write % TRI_DAC_BYTES] = v & 0x3F;',
     'if (f->dac_write != 767) f->dac[f->dac_write % TRI_DAC_BYTES] = v & 0x3F;'),
)


def source():
    text = ''.join(f'#include "{h}"\n' for h in HEADERS)
    text += f'#include "{HOST}"\n'
    for name, signature in SIGNATURES.items():
        text += f'_Static_assert(__builtin_types_compatible_p(__typeof__(&{name}), {signature}), "{name}");\n'
    for expr, typ in TYPES.items():
        text += f'_Static_assert(__builtin_types_compatible_p(__typeof__({expr}), {typ}), "{expr}");\n'
    checks = list(CONSTANTS.items())
    for typ, (size, members) in LAYOUTS.items():
        checks.append((f'sizeof({typ})', size))
        checks += [(f'__builtin_offsetof({typ}, {member})', off) for member, off in members.items()]
    text += '''static void emit(const char *s, u32 n) {
    __asm__ volatile ("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static int contracts(void) {
'''
    for expr, value in checks:
        msg = f'RED {expr}\n'
        literal = msg.replace('\n', '\\n')
        text += f'    if (!equal({expr}, (u32)({value}))) {{ emit("{literal}", {len(msg)}); return 1; }}\n'
    text += '''    if (selftest()) { emit("RED fake foundation\\n", 20); return 1; }
    emit("GREEN\\n", 6); return 0;
}
void _start(void) {
    int rc = contracts();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
'''
    return text


def build(work, tree):
    src = work / 'contracts.c'
    src.write_text(source())
    exe = work / 'contracts'
    subprocess.run(['gcc', *FLAGS, '-I' + str(tree), '-I' + str(tree / 'include'), *include_flags(ROOT),
                    '-nostdlib', '-static', '-no-pie', str(src),
                    str(tree / FAKE_C), '-o', str(exe)], cwd=ROOT, check=True)
    return exe


def surface_abi(work, compiler, surface_id=5, expected_failure=False):
    # Preinclude the copied public header so the product TU uses that value.
    # This negative compile test is explicitly required by PACK.fix3 N1;
    # it is separate from the executable mutants below.
    header = work / 'os32_surface.h'
    original = (ROOT / 'sdk/include/os32/os32_surface.h').read_text()
    old = '#define OS32_SURFACE_TRIDENT 5'
    assert original.count(old) == 1
    header.write_text(original.replace(old, f'#define OS32_SURFACE_TRIDENT {surface_id}'))
    public = work / 'public.c'
    public.write_text(f'#include "{header}"\n')
    subprocess.run([compiler, '-m32', '-std=c89', '-pedantic-errors',
                    '-fsyntax-only', str(public)], check=True)
    src = work / 'surface_abi.c'
    src.write_text(f'#include "{header}"\n'
                   f'#include "{ROOT / "exec/surface_query.c"}"\n')
    result = subprocess.run([compiler, *FLAGS, '-I' + str(ROOT),
                             *include_flags(ROOT), '-fsyntax-only', str(src)],
                            capture_output=True, text=True)
    if expected_failure:
        assert result.returncode != 0 and 'abi_surface_trident' in result.stderr, result
        print(f'MUTATE RED surface SDK/backend mismatch (compile assertion, {compiler})', flush=True)
    else:
        assert result.returncode == 0, result.stderr
        print(f'PASS public C89 header and product surface ABI assertion ({compiler})', flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--mutate', action='store_true')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='trident-contracts-') as tmp:
        work = Path(tmp)
        cross = Path(os.environ.get('CROSS_DIR', '/home/hight/opt/cross')) / 'bin/i386-elf-gcc'
        for compiler in ('gcc', str(cross)):
            surface_abi(work, compiler)
            if args.mutate:
                surface_abi(work, compiler, surface_id=6, expected_failure=True)
            for header in HEADERS:
                src = work / 'standalone.c'
                src.write_text(f'#include "{header}"\n')
                subprocess.run([compiler, *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                                '-c', str(src), '-o', str(work / 'standalone.o')], check=True)
            src.write_text(source())
            subprocess.run([compiler, *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                            '-c', str(src), '-o', str(work / 'types.o')], check=True)
            subprocess.run([compiler, *FLAGS, '-I' + str(ROOT), *include_flags(ROOT),
                            '-c', str(ROOT / FAKE_C), '-o', str(work / 'fake.o')], check=True)
            print(f'PASS standalone headers, all function signatures, fake ({compiler})', flush=True)
        runners = os.environ.get('HOST32_RUNNERS', 'qemu').split()
        assert runners, 'no host32 runners'
        exe = build(work, ROOT)
        for runner in runners:
            result = host32.run([str(exe)], runner=runner, capture_output=True)
            assert result.returncode == 0 and result.stdout == b'GREEN\n', result
            print(f'PASS constant/layout contracts and fake selftest ({runner})', flush=True)
        if args.mutate:
            tree = work / 'tree'
            for path in (*HEADERS, FAKE_C, HOST, 'exec/surface_limits.h',
                         'mm/paging.h', 'mm/physmem.h'):
                dest = tree / path
                dest.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(ROOT / path, dest)
            for name, path, old, new in MUTATIONS:
                target = tree / path
                original = target.read_text()
                assert original.count(old) == 1, (name, original.count(old))
                target.write_text(original.replace(old, new))
                try:
                    exe = build(work, tree)  # compile failure is never RED
                    for runner in runners:
                        result = host32.run([str(exe)], runner=runner, capture_output=True)
                        assert result.returncode == 1 and result.stdout.startswith(b'RED '), (name, result)
                        print(f'MUTATE RED {name} ({runner}): {result.stdout.decode().strip()}', flush=True)
                finally:
                    target.write_text(original)
            exe = build(work, tree)
            for runner in runners:
                result = host32.run([str(exe)], runner=runner, capture_output=True)
                assert result.returncode == 0 and result.stdout == b'GREEN\n', result
            print('GREEN restored contracts', flush=True)
    return 0


def run(ctx):
    env = dict(os.environ, HOST32_RUNNERS=' '.join(ctx.runners))
    return subprocess.call(['python3', '-B', 'tools/tests/test_trident_contracts.py',
                            *(['--mutate'] if ctx.mutate else [])], cwd=ctx.root, env=env)


if __name__ == '__main__':
    raise SystemExit(main())
