/* Source notices retained from trident_mode.hwb:3–48.
 * Copyright 1991-1999 by The XFree86 Project, Inc.
 * Copyright 1990,91 by Thomas Roell, Dinkelscherben, Germany.
 * vgaHW.c license: xorg-server-21.1.13 COPYING:54-76
 * Copyright (C) 1994-2003 The XFree86 Project, Inc.  All Rights Reserved.
 * Copyright (C) Colin Harrison 2005-2008
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy of
 * this software and associated documentation files (the "Software"), to deal in
 * the Software without restriction, including without limitation the rights to
 * use, copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 * of the Software, and to permit persons to whom the Software is furnished to do
 * so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FIT-
 * NESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL THE
 * XFREE86 PROJECT BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
 * WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Except as contained in this notice, the name of the XFree86 Project shall not
 * be used in advertising or otherwise to promote the sale, use or other dealings
 * in this Software without prior written authorization from the XFree86 Project.
 * Copyright 1992-2003 by Alan Hourihane, North Wales, UK.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of Alan Hourihane not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  Alan Hourihane makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * ALAN HOURIHANE DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL ALAN HOURIHANE BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 *
 */
/* Internal contract: TRIDENT_S3_DESIGN §§2–4 (9682 / Ra266 only).
 * Register sequences: MIT X.Org trident_driver-1.4.0.c:616–775,
 * 940–1136,1201–1247,2919–2975,3343–3348,3471–3481 and
 * userland/tests/hw/trident_mode.hwb. Manufacturer sync reference:
 * NEC LCD172VXM/192VXM manual p.20; 9682 register manual unavailable.
 * No public KAPI/SDK ABI. All int operations return 0 or a negative rc.
 */
#ifndef TRIDENT_H
#define TRIDENT_H
#include <stdbool.h>
#include "types.h"
#include "memmap.h"
#include "gfx/palette.h"

#define TRI_PCI_VENDOR 0x1023u
#define TRI_PCI_DEVICE 0x9660u
#define TRI_CHIP_9682 0x10u
#define TRI_PCI_BUS 0u
#define TRI_PCI_SLOT 8u
#define TRI_PCI_FUNCTION 0u
#define TRI_PCI_COMMAND 0x0002u
/* Device windows are identity mapped; memmap.h owns their addresses. */
#define TRI_BAR0_PHYS MEM_TRIDENT_LFB_VA
#define TRI_BAR1_PHYS MEM_TRIDENT_REGS_VA
#define TRI_BAR0_DECODE_BYTES 0x00400000u
#define TRI_BAR1_DECODE_BYTES 0x00010000u
#define TRI_REG_MAPPED_BYTES 0x00009000u /* covers 0..83C8, 9 pages */
#define TRI_WIDTH 640u
#define TRI_HEIGHT 480u
#define TRI_PITCH 640u
#define TRI_DISPLAY_BYTES 307200u
#define TRI_DISPLAY_PAGES 75u
#define TRI_PAGE_BYTES 4096u
#define TRI_VRAM_SCRATCH_BYTES 8192u /* 1024 boundary words + one page */
#define TRI_DAC_COLORS 256u
#define TRI_DAC_BYTES 768u
#define TRI_SAVED_BYTES 852u
#define TRI_SAVE_MCLK 0u /* independent of Init's 80MHz writes */
#define TRI_SR_INDEX 0x3C4u
#define TRI_GR_INDEX 0x3CEu
#define TRI_CR_COLOR 0x3D4u
#define TRI_CR_MONO 0x3B4u
#define TRI_AR_INDEX 0x3C0u
#define TRI_AR_DATA 0x3C1u
#define TRI_MISC_READ 0x3CCu
#define TRI_MISC_WRITE 0x3C2u
#define TRI_DAC_MASK 0x3C6u
#define TRI_DAC_READ_INDEX 0x3C7u
#define TRI_DAC_WRITE_INDEX 0x3C8u
#define TRI_DAC_DATA 0x3C9u
#define TRI_MCLK_LO 0x43C6u
#define TRI_MCLK_HI 0x43C7u
#define TRI_PLL_LO 0x43C8u
#define TRI_PLL_HI 0x43C9u
#define TRI_EXT_DAC_DATA 0x83C6u
#define TRI_EXT_DAC_INDEX 0x83C8u
#define TRI_PORT_MODE 0x68u
#define TRI_PORT_FF 0x6Au
#define TRI_PORT_ROUTE 0xFACu

/* Flat save slots follow the script, including the unused MCLK reservation. */
enum trident_save_slot {
    TRI_SAVE_DAC = 0, TRI_SAVE_MISC = 768, TRI_SAVE_CR = 769,
    TRI_SAVE_AR = 794, TRI_SAVE_GR = 815, TRI_SAVE_SR = 824,
    TRI_SAVE_SR0E = 828, TRI_SAVE_CR13 = 829, TRI_SAVE_CR21 = 830,
    TRI_SAVE_CR1E = 831, TRI_SAVE_CR27 = 832, TRI_SAVE_CR2B = 833,
    TRI_SAVE_CR2F = 834, TRI_SAVE_CR2A = 835, TRI_SAVE_CR3A = 836,
    TRI_SAVE_CR29 = 837, TRI_SAVE_CR38 = 838, TRI_SAVE_CR36 = 839,
    TRI_SAVE_CR39 = 840, TRI_SAVE_CR55 = 841, TRI_SAVE_CR50 = 842,
    TRI_SAVE_GR0F = 843, TRI_SAVE_GR2F = 844, TRI_SAVE_HIDDEN_DAC = 845,
    TRI_SAVE_CLOCK_MISC = 846, TRI_SAVE_PLL_LO = 847, TRI_SAVE_PLL_HI = 848,
    TRI_SAVE_MCLK_LO = 849, TRI_SAVE_MCLK_HI = 850, TRI_SAVE_SR0D = 851
};
enum trident_rc {
    TRI_OK = 0, TRI_E_INVAL = -1, TRI_E_DEVICE = -2,
    TRI_E_RESOURCE = -3, TRI_E_MAP = -4, TRI_E_SURFACE = -5,
    TRI_E_ACCESS = -6, TRI_E_RESTORE = -7, TRI_E_CAP_TOO_SMALL = -8,
    TRI_E_BUSY = -9, TRI_E_STATE = -10, TRI_E_MODULE = -11
};
/* step = last completed stage, failed_step/last_rc = first failed operation.
 * Lifecycle owns these fields; primitive functions return rc without clearing
 * an earlier error. restore_rc/disable_rc retain both recovery outcomes. */
enum trident_step {
    TRI_STEP_NONE = 0, TRI_STEP_RESOURCES = 1, TRI_STEP_PIN = 2,
    TRI_STEP_ARM = 3, TRI_STEP_IDENTIFY = 4, TRI_STEP_SAVE = 5,
    TRI_STEP_PC98_INIT = 6, TRI_STEP_CAPACITY_PLAN = 7, TRI_STEP_MODE = 8,
    TRI_STEP_ENABLE = 9, TRI_STEP_VERIFY = 10, TRI_STEP_CLIENT = 11,
    TRI_STEP_ACTIVE = 12, TRI_STEP_RESTORE = 13, TRI_STEP_LOCK = 14,
    TRI_STEP_DISABLE = 15, TRI_STEP_CLOSED = 16
};
enum trident_upper { TRI_UPPER_UNKNOWN = 0, TRI_UPPER_MIRROR = 1,
                     TRI_UPPER_INDEPENDENT = 2 };
/* Byte/word/dword callbacks are distinct so OUTW never becomes two OUTBs.
 * Offsets are relative to the resource, except PORT8 uses a real I/O port.
 * Failed reads leave *value unchanged. No retries, readback or waiting.
 * regs.c owns the production implementation, fake uses exactly this seam.
 * All product register/LFB/port accesses, including present, use this seam. */
enum trident_space { TRI_SPACE_REGS = 0, TRI_SPACE_LFB = 1 };
struct trident_io {
    int (*read8)(void *, enum trident_space, u32, u8 *);
    int (*read16)(void *, enum trident_space, u32, u16 *);
    int (*read32)(void *, enum trident_space, u32, u32 *);
    int (*write8)(void *, enum trident_space, u32, u8);
    int (*write16)(void *, enum trident_space, u32, u16);
    int (*write32)(void *, enum trident_space, u32, u32);
    int (*port8)(void *, u16, u8);
    /* One LFB row span: aligned offset/source, count in dwords; validate the
     * whole range before writing. No readback. Scalar write8 handles edges. */
    int (*write_span32)(void *, u32 off, const u32 *src, u32 count);
};
struct trident_pci_info {
    u16 vendor, device, command;
    u8 bus, slot, function, revision;
    u32 bar0, bar1; /* raw config values, preserve type bits for validation */
};
struct trident_saved { u8 value[TRI_SAVED_BYTES]; u16 vga_base; bool valid; };
struct trident_resources {
    /* Kernel owns reservations, maps and surfaces; module borrows forever
     * after arm. owner is DEVICE reservation owner, not exec display owner. */
    volatile u8 *regs, *lfb;
    u8 *client;
    u32 owner, bar0_rid, bar1_rid, decode_bytes, mapped_bytes;
    u32 display_sid, client_sid, client_bytes;
    u32 bar0_phys, bar1_phys, regs_bytes;
    struct trident_pci_info pci;
    bool ready;
};
struct trident_device {
    const struct trident_resources *res;
    struct trident_saved saved;
    u32 declared_bytes, verified_bytes, step;
    int last_rc, restore_rc, disable_rc;
    bool init_done, first_init_done, active, returning, armed, leave_done, closed;
    /* New device is entirely zero; regs_bind installs io without hardware I/O.
     * pin/arm must precede identify (even index selection writes hardware).
     * saved.valid commits only after a complete save and is never overwritten.
     * lifecycle owns init_done (sets only after pc98_init returns success)
     * and first_init_done (complete first init). pc98_init does not set it.
     * Both persist across leave/close, as do verified_bytes and the maps. */
    const struct trident_io *io;
    void *io_opaque;
    u32 failed_step, display_owner; /* arm-time diagnostic only, never a filter */
    u8 chip, spr, reference; /* identify owns chip; capacity_plan owns spr;
                             * mode_640 reads reference after PC98 Init (H:148-151). */
    bool pinned, declared_unknown;
    enum trident_upper upper;
    /* RAM loan for first VRAM verification only; lifecycle allocates/frees.
     * Emergency restoration never uses scratch or allocates. */
    u32 *scratch;
    u32 scratch_bytes;
};

/* probe.c: config-only validation before arm; indexed identify after arm. */
int trident_pci_validate(const struct trident_pci_info *pci);
int trident_identify(struct trident_device *d);
/* resources.c (core): master CR3/live AS=0; no indexed access/BAR writes.
 * Decode reservations survive rollback. Before arm roll back surfaces/client
 * and successful maps; after arm rollback returns BUSY. ready only on commit. */
int trident_boot_reserve(void);
const struct trident_resources *trident_resources_get(void);
int trident_resources_rollback(bool armed);
/* regs.c: bind is write-free; these wrappers keep exact width and propagate rc.
 * AR/protect use vga_base (3B4/3D4); status port = vga_base+6.
 * indexed_write is one 16-bit OUTW, indexed_read selects with one OUTB.
 * vgaHW operations (protect/CRTC lock and D:636/643) use two 8bit writes,
 * never indexed_write. Base selection follows vgaHW.c:1786–1789,1021–1025. */
int trident_regs_bind(struct trident_device *d);
int trident_read8(struct trident_device *, enum trident_space, u32, u8 *);
int trident_read16(struct trident_device *, enum trident_space, u32, u16 *);
int trident_read32(struct trident_device *, enum trident_space, u32, u32 *);
int trident_write8(struct trident_device *, enum trident_space, u32, u8);
int trident_write16(struct trident_device *, enum trident_space, u32, u16);
int trident_write32(struct trident_device *, enum trident_space, u32, u32);
int trident_port8(struct trident_device *, u16, u8);
int trident_indexed_read(struct trident_device *, u16 index_port, u8 index, u8 *);
int trident_indexed_write(struct trident_device *, u16 index_port, u8 index, u8);
int trident_ar_read(struct trident_device *, u16 vga_base, u8 index, u8 *);
int trident_ar_write(struct trident_device *, u16 vga_base, u8 index, u8);
int trident_hidden_dac_read(struct trident_device *, u8 *);
int trident_hidden_dac_write(struct trident_device *, u8);
/* H:493: 83C8=4, then read-modify-write 83C6. */
int trident_ext_dac_update(struct trident_device *, u8 mask, u8 bits);
int trident_protect(struct trident_device *, u16 vga_base, bool on);
int trident_crtc_lock(struct trident_device *, u16 vga_base, bool lock);
/* pc98_init.c: full D:616–692, including protect and SR0C alternate branch.
 * Protect and unlock/lock use the Save-time base, as in the reference.
 * The script fixes these at 3D4 (same when boot MISC bit0=1).
 * Explicit AR and Trident CR accesses stay at 3D4. */
int trident_pc98_init(struct trident_device *d);
/* state_save.c/default_dac.c/state_restore.c: 852 slots, nonprimary,
 * no font/text planes, default MCLK not saved, SR0E restore XOR 02.
 * Save selects base from MISC at save time (vgaHW.c:1021–1025). Restore
 * entry protect/Trident uses current MISC; VGA Restore writes saved MISC
 * then uses its base for AR/CRTC and exit protect (vgaHW.c:845–865;
 * trident_dac.c:781–810, D:1111–1136). */
int trident_state_save(struct trident_device *d);
extern const u8 trident_default_dac[TRI_DAC_BYTES];
int trident_state_restore(struct trident_device *d);
/* vram.c: plan reads SPR/CR C1 after Init; full table + unknown diagnostic.
 * verify runs after Enable, before publication/leases. Uses scratch, records
 * continuous capacity and upper status; restores all probes in reverse order.
 * >= DISPLAY_BYTES succeeds, not a requirement for 2MiB or 4MiB. */
int trident_capacity_plan(struct trident_device *d);
int trident_vram_verify(struct trident_device *d);
/* mode_640.c excludes Enable; lifecycle calls Enable once after ModeInit.
 * Mode/route AR and protect use 3D4 per the script (H:548–554).
 * The reference retains the Save-time base (vgaHW.c:1021; D:704–727,740–767);
 * boot MISC bit0=0 gives 3B4 there even after Init MISC|=C3.
 * Route 0xFAC also belongs to pc98_route.c; their relationship is set in 3c. */
int trident_mode_640(struct trident_device *d);
int trident_route_enable(struct trident_device *d);
int trident_route_disable(struct trident_device *d);
/* RAM client is res->client/client_bytes, pitch TRI_PITCH. Present clips then
 * validates source/LFB ends and copies rows via write_span32 (write8 edges),
 * with no VRAM readback. Invalid palette interval is INVAL. */
int trident_present(struct trident_device *, int x, int y, int w, int h);
/* HAL RGB components use GFX_PALETTE_COMPONENT_BITS (currently 4, up to 8
 * per component for 32bit color), not a backend-fixed input width.
 * palette.c and lifecycle's default palette MUST both call this shared
 * conversion with GFX_PALETTE_COMPONENT_BITS. W is in [4,8]: mask to W bits;
 * W < 6 replicates the high input bits into the remaining low DAC bits
 * (W=4: x<<2 | x>>2), W=6 is unchanged, W>6 takes the high six (W=8: x>>2).
 * This pure contract helper performs no I/O; saved/default DAC register
 * images are already in DAC units and must not be converted again. */
STATIC_ASSERT(GFX_PALETTE_COMPONENT_BITS >= 4 && GFX_PALETTE_COMPONENT_BITS <= 8, trident_palette_component_bits_range);
static inline u8 trident_palette_dac6(u8 component, u32 component_bits)
{
    u32 bits = component_bits;
    u32 x = component & ((1u << bits) - 1u);
    if (bits < 6)
        return (u8)((x << (6 - bits)) | (x >> (2 * bits - 6)));
    return (u8)(x >> (bits - 6));
}
int trident_palette(struct trident_device *, int first, int count, const u8 *rgb);
/* lifecycle.c: ScreenInit=Save→Init→plan→Mode→Enable→verify→palette/client once;
 * later init=Mode→Enable→palette/client, enter=Mode→Enable (active enter is no-op).
 * Palette defaults come from gfx/palette.c shadow.
 * leave=Restore→lock→Disable, close=Disable→Restore→lock only when active;
 * close after leave and duplicate close touch no hardware. Never re-save.
 * Recovery tries both operations, keeps both rc, no allocation/log/tick/GE.
 * A failed first init returns its recorded rc on later init; restore failure
 * prevents automatic reentry. returning guards recursive restoration.
 * The caller always writes the TVRAM fault, regardless of recovery rc. */
int trident_screen_init(struct trident_device *d);
int trident_enter(struct trident_device *d);
int trident_leave(struct trident_device *d);
int trident_close(struct trident_device *d);
/* restore98 touches hardware only while active (vtSema, D:3333). */
int trident_restore98(void *opaque);
#endif
