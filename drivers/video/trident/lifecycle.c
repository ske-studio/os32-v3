/* MIT reference notices in trident.h. trident_driver-1.4.0.c:1201-1247,
 * 2920-2969,3333-3348,3471-3488; vgaHW.c:1786-1789.
 * PM 2026-11-06: PreInit capacity read precedes Save/PC98 Init, unlike the
 * original S3 contract. PM fix6: every HAL init is ScreenInit, so Save and
 * PC98 Init repeat (D:3472-3487); only VRAM measurement remains first-only.
 * 0xFAC is shared with pc98_route.c (mask 3): Trident owns values 2/0 while
 * active/returning. Keep Enable/Disable's unconditional OUTB and ordering;
 * route_set's conditional write/readback cannot replace this sequence.
 * Cooperative switching serializes the backends. No 98 drawing here.
 */
#include "trident.h"
#include "mm/kmalloc.h"

static int failed(struct trident_device *d, u32 step, int rc)
{
    if (rc && !d->last_rc) {
        d->failed_step = step;
        d->last_rc = rc;
    }
    return rc;
}

#define STAGE(stage, call) do { \
    int rc = (call); \
    if (rc) return failed(d, (stage), rc); \
    d->step = (stage); \
} while (0)

static int mode_enable(struct trident_device *d)
{
    /* vtSema covers partial mode programming, not just a published screen. */
    d->active = true;
    d->closed = d->leave_done = false;
    STAGE(TRI_STEP_MODE, trident_mode_640(d));
    STAGE(TRI_STEP_ENABLE, trident_route_enable(d));
    return 0;
}

/* TridentLoadPalette, trident_dac.c:1259-1282: each mask/index/RGB OUTB
 * has two status reads (DACDelay), including the initial HAL palette. */
static int dac_write(struct trident_device *d, u32 port, u8 value)
{
    u8 ignored;
    int rc = trident_write8(d, TRI_SPACE_REGS, port, value);
    if (rc) return rc;
    rc = trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &ignored);
    if (rc) return rc;
    return trident_read8(d, TRI_SPACE_REGS, d->cur_vga_base + 6u, &ignored);
}

#define CLIENT_WRITE(call) do { int rc = (call); if (rc) return rc; } while (0)

static int client_init(struct trident_device *d)
{
    const PaletteEntry *sys = palette_get_all();
    u8 rgb[TRI_DAC_BYTES];
    u32 i = 0;
    /* System colors use HAL component units; cube/gray use DAC units. */
    for (; i < GFX_PAL_ENTRIES(GFX_FMT_PLANAR4_BPP); ++i) {
        rgb[i * 3] = trident_palette_dac6(sys[i].r, GFX_PALETTE_COMPONENT_BITS);
        rgb[i * 3 + 1] = trident_palette_dac6(sys[i].g, GFX_PALETTE_COMPONENT_BITS);
        rgb[i * 3 + 2] = trident_palette_dac6(sys[i].b, GFX_PALETTE_COMPONENT_BITS);
    }
    const u32 dac_max = (1u << 6) - 1u;
    for (u32 r = 0; r < GFX_PALETTE_CUBE_LEVELS; ++r)
        for (u32 g = 0; g < GFX_PALETTE_CUBE_LEVELS; ++g)
            for (u32 b = 0; b < GFX_PALETTE_CUBE_LEVELS; ++b, ++i) {
                rgb[i * 3] = r * dac_max / (GFX_PALETTE_CUBE_LEVELS - 1u);
                rgb[i * 3 + 1] = g * dac_max / (GFX_PALETTE_CUBE_LEVELS - 1u);
                rgb[i * 3 + 2] = b * dac_max / (GFX_PALETTE_CUBE_LEVELS - 1u);
            }
    for (u32 gray = 0; i < TRI_DAC_COLORS; ++i, ++gray) {
        u32 gray8 = GFX_PALETTE_GRAY_FIRST + gray * GFX_PALETTE_GRAY_STEP;
        u8 v = (gray8 * dac_max + 127u) / 255u;
        for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c) rgb[i * 3 + c] = v;
    }
    for (i = 0; i < TRI_DAC_COLORS; ++i) {
        CLIENT_WRITE(dac_write(d, TRI_DAC_MASK, 0xFF));
        CLIENT_WRITE(dac_write(d, TRI_DAC_WRITE_INDEX, i));
        for (u32 c = 0; c < GFX_RGB_COMPONENTS; ++c)
            CLIENT_WRITE(dac_write(d, TRI_DAC_DATA, rgb[i * 3 + c]));
    }
    for (i = 0; i < TRI_DISPLAY_BYTES; ++i) d->res->client[i] = 0;
    return 0;
}

int trident_screen_init(struct trident_device *d)
{
    if (!d || !d->res || !d->io) return TRI_E_INVAL;
    if (d->last_rc) return d->last_rc;
    if (!d->armed || !d->pinned || !d->res->ready) return TRI_E_STATE;
    if (d->returning) return TRI_E_BUSY;
    if (!d->first_init_done) {
        u8 misc;
        STAGE(TRI_STEP_IDENTIFY, trident_identify(d));
        STAGE(TRI_STEP_CAPACITY_PLAN, trident_read8(d, TRI_SPACE_REGS, TRI_MISC_READ, &misc));
        d->cur_vga_base = (misc & 1u) ? TRI_CR_COLOR : TRI_CR_MONO;
        STAGE(TRI_STEP_CAPACITY_PLAN, trident_capacity_plan(d));
    }
    d->active = true; /* even a failed Save can require Disable */
    d->closed = d->leave_done = false;
    STAGE(TRI_STEP_SAVE, trident_state_save(d));
    d->init_done = false;
    STAGE(TRI_STEP_PC98_INIT, trident_pc98_init(d));
    d->init_done = true;
    int rc = mode_enable(d);
    if (rc) return rc;
    if (!d->first_init_done) {
        d->scratch = kmalloc(TRI_VRAM_SCRATCH_BYTES);
        if (!d->scratch) return failed(d, TRI_STEP_VERIFY, TRI_E_RESOURCE);
        d->scratch_bytes = TRI_VRAM_SCRATCH_BYTES;
        rc = trident_vram_verify(d);
        kfree(d->scratch);
        d->scratch = NULL;
        d->scratch_bytes = 0;
        if (rc) return failed(d, TRI_STEP_VERIFY, rc);
        d->step = TRI_STEP_VERIFY;
    }
    rc = client_init(d);
    if (rc) return failed(d, TRI_STEP_CLIENT, rc);
    d->step = TRI_STEP_CLIENT;
    d->first_init_done = true;
    d->step = TRI_STEP_ACTIVE;
    return 0;
}

int trident_enter(struct trident_device *d)
{
    if (!d) return TRI_E_INVAL;
    if (d->last_rc) return d->last_rc;
    if (d->returning) return TRI_E_BUSY;
    if (d->active) return 0;
    if (!d->first_init_done) return TRI_E_STATE;
    int rc = mode_enable(d);
    if (!rc) d->step = TRI_STEP_ACTIVE;
    return rc;
}

static int restore_lock(struct trident_device *d)
{
    int restore = 0;
    if (d->saved.valid) {
        restore = trident_state_restore(d);
        if (restore) failed(d, TRI_STEP_RESTORE, restore);
        else d->step = TRI_STEP_RESTORE;
    }
    /* CloseScreen locks under vtSema even when Save never completed (D:3348). */
    int lock = trident_crtc_lock(d, d->cur_vga_base, true);
    if (lock) failed(d, TRI_STEP_LOCK, lock);
    else d->step = TRI_STEP_LOCK;
    if (!restore) restore = lock;
    d->restore_rc = restore;
    return restore;
}

static int disable(struct trident_device *d)
{
    d->disable_rc = trident_route_disable(d);
    if (d->disable_rc) failed(d, TRI_STEP_DISABLE, d->disable_rc);
    else d->step = TRI_STEP_DISABLE;
    return d->disable_rc;
}

static int return98(struct trident_device *d, bool close)
{
    if (!d) return TRI_E_INVAL;
    if (d->returning) return TRI_E_BUSY;
    if (!d->active) {
        if (close) { d->closed = true; d->step = TRI_STEP_CLOSED; }
        return d->restore_rc ? d->restore_rc : d->disable_rc;
    }
    d->returning = true;
    int first, second;
    if (close) {
        first = disable(d);
        second = restore_lock(d);
    } else {
        first = restore_lock(d);
        second = disable(d);
    }
    d->active = false; /* state_restore requires vtSema until it returns */
    d->returning = false;
    d->leave_done = !close;
    d->closed = close;
    if (close && !first && !second) d->step = TRI_STEP_CLOSED;
    return first ? first : second;
}

int trident_leave(struct trident_device *d) { return return98(d, false); }
int trident_close(struct trident_device *d) { return return98(d, true); }
int trident_restore98(void *opaque) { return trident_close(opaque); }
