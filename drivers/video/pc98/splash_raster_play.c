/* Shared S4 finite-poll scan; no clocks or diagnostics in visible rows. */
#include "splash_raster_play.h"
#include "pc98.h"
#include "palette.h"
#include "io.h"

#define PL_PLAY_POLL_LIMIT 1000000u

/* Select a fully expanded column before polling its edge. Every emitter
 * writes offsets in ascending order; no loop or color decoding inside H. */
typedef void (*PlV2Emit)(const struct PlV2Row *);
#define PL_OUT_0 (void)r
#define PL_OUT_1 PL_OUT_0; outp(r->port[0], r->value[0])
#define PL_OUT_2 PL_OUT_1; outp(r->port[1], r->value[1])
#define PL_OUT_3 PL_OUT_2; outp(r->port[2], r->value[2])
#define PL_OUT_4 PL_OUT_3; outp(r->port[3], r->value[3])
#define PL_OUT_5 PL_OUT_4; outp(r->port[4], r->value[4])
#define PL_OUT_6 PL_OUT_5; outp(r->port[5], r->value[5])
#define PL_OUT_7 PL_OUT_6; outp(r->port[6], r->value[6])
#define PL_OUT_8 PL_OUT_7; outp(r->port[7], r->value[7])
#define PL_OUT_9 PL_OUT_8; outp(r->port[8], r->value[8])
#define PL_OUT_10 PL_OUT_9; outp(r->port[9], r->value[9])
#define PL_OUT_11 PL_OUT_10; outp(r->port[10], r->value[10])
#define PL_OUT_12 PL_OUT_11; outp(r->port[11], r->value[11])
#define PL_OUT_13 PL_OUT_12; outp(r->port[12], r->value[12])
#define PL_OUT_14 PL_OUT_13; outp(r->port[13], r->value[13])
#define PL_OUT_15 PL_OUT_14; outp(r->port[14], r->value[14])
#define PL_OUT_16 PL_OUT_15; outp(r->port[15], r->value[15])
#define PL_OUT_17 PL_OUT_16; outp(r->port[16], r->value[16])
#define PL_OUT_18 PL_OUT_17; outp(r->port[17], r->value[17])
#define PL_OUT_19 PL_OUT_18; outp(r->port[18], r->value[18])
#define PL_OUT_20 PL_OUT_19; outp(r->port[19], r->value[19])
#define PL_OUT_21 PL_OUT_20; outp(r->port[20], r->value[20])
#define PL_OUT_22 PL_OUT_21; outp(r->port[21], r->value[21])
#define PL_OUT_23 PL_OUT_22; outp(r->port[22], r->value[22])
#define PL_OUT_24 PL_OUT_23; outp(r->port[23], r->value[23])
#define PL_OUT_25 PL_OUT_24; outp(r->port[24], r->value[24])
#define PL_OUT_26 PL_OUT_25; outp(r->port[25], r->value[25])
#define PL_OUT_27 PL_OUT_26; outp(r->port[26], r->value[26])
#define PL_OUT_28 PL_OUT_27; outp(r->port[27], r->value[27])
#define PL_OUT_29 PL_OUT_28; outp(r->port[28], r->value[28])
#define PL_OUT_30 PL_OUT_29; outp(r->port[29], r->value[29])
#define PL_OUT_31 PL_OUT_30; outp(r->port[30], r->value[30])
#define PL_EMITTER(n) static void pl_emit##n(const struct PlV2Row *r) { PL_OUT_##n; }
PL_EMITTER(0) PL_EMITTER(1) PL_EMITTER(2) PL_EMITTER(3) PL_EMITTER(4) PL_EMITTER(5) PL_EMITTER(6) PL_EMITTER(7)
PL_EMITTER(8) PL_EMITTER(9) PL_EMITTER(10) PL_EMITTER(11) PL_EMITTER(12) PL_EMITTER(13) PL_EMITTER(14) PL_EMITTER(15)
PL_EMITTER(16) PL_EMITTER(17) PL_EMITTER(18) PL_EMITTER(19) PL_EMITTER(20) PL_EMITTER(21) PL_EMITTER(22) PL_EMITTER(23)
PL_EMITTER(24) PL_EMITTER(25) PL_EMITTER(26) PL_EMITTER(27) PL_EMITTER(28) PL_EMITTER(29) PL_EMITTER(30) PL_EMITTER(31)
static const PlV2Emit emit_table[] = {
    pl_emit0, pl_emit1, pl_emit2, pl_emit3, pl_emit4, pl_emit5, pl_emit6, pl_emit7,
    pl_emit8, pl_emit9, pl_emit10, pl_emit11, pl_emit12, pl_emit13, pl_emit14, pl_emit15,
    pl_emit16, pl_emit17, pl_emit18, pl_emit19, pl_emit20, pl_emit21, pl_emit22, pl_emit23,
    pl_emit24, pl_emit25, pl_emit26, pl_emit27, pl_emit28, pl_emit29, pl_emit30, pl_emit31,
};
STATIC_ASSERT(sizeof(emit_table) / sizeof(emit_table[0]) ==
              OSRI_DEFAULT_OUT_BUDGET + 1u, pl_emit_table_len);
#undef PL_EMITTER

static int play_vsync(int high)
{
    for (u32 i = 0; i < PL_PLAY_POLL_LIMIT; i++)
        if (!!(inp(GDC_GFX_STAT) & GDC_STAT_VSYNC) == high) return 0;
    return -1;
}

/* Low and high waits each inspect VSYNC in the very same status IN.
 * A long column may lose H edges; a new VBLANK always terminates the frame. */
static int play_edge(const struct PlV2Row *r, PlV2Emit emit)
{
    int armed = 0;
    for (u32 i = 0; i < PL_PLAY_POLL_LIMIT; i++) {
        u32 status = inp(GDC_GFX_STAT);
        if (status & GDC_STAT_VSYNC) return PL_FRAME_DROP;
        if (!(status & GDC_STAT_HBLANK)) {
            if (!armed) {
                armed = 1;
                if (r->count) outp(PAL_IDX_PORT, r->first);
            }
        } else if (armed) {
            emit(r);
            return PL_FRAME_OK;
        }
    }
    return PL_FRAME_NO_SYNC;
}

int pl_v2_frame(struct SplashRaster *s, const struct OsriImage *image,
                 const struct PlV2Plan *plan)
{
    if (!s || !image || !plan || !_irq_enabled() ||
        !s->checked || !s->dynamic_ok) return PL_FRAME_NO_SYNC;
    /* Exactly one new VBLANK per call, even when the following scan drops. */
    if (play_vsync(0) || play_vsync(1)) return PL_FRAME_NO_SYNC;
    unsigned int flags = irq_save();
    int rc = PL_FRAME_DROP;
    /* Protect F acquisition as well as visible rows. IRQs were open while
     * waiting for this new VBLANK; a delayed CLI may already have lost it. */
    if (!(inp(GDC_GFX_STAT) & GDC_STAT_VSYNC)) goto play_done;
    for (u32 i = 0; i < OSRI_COLORS; i++) {
        u16 rgb = image->pal[i];
        outp(PAL_IDX_PORT, i);
        outp(PAL_G_PORT, (rgb >> 4) & GFX_PALETTE_COMPONENT_MASK);
        outp(PAL_R_PORT, (rgb >> 8) & GFX_PALETTE_COMPONENT_MASK);
        outp(PAL_B_PORT, rgb & GFX_PALETTE_COMPONENT_MASK);
    }
    /* If initial colors/IRQ crossed F, its origin is lost. Do not rebase. */
    if (!(inp(GDC_GFX_STAT) & GDC_STAT_VSYNC)) goto play_done;
    rc = PL_FRAME_NO_SYNC;
    if (play_vsync(0)) goto play_done;
    for (u32 k = 1; k <= s->active0_lines + SPLASH_RASTER_VISIBLE_LINES; k++) {
        u32 y = k > s->active0_lines ? k - s->active0_lines : 0;
        const struct PlV2Row *r = &plan->row[y < SPLASH_RASTER_VISIBLE_LINES ? y : 0];
        PlV2Emit emit = emit_table[r->count];
        rc = play_edge(r, emit);
        if (rc != PL_FRAME_OK) break;
    }
play_done:
    irq_restore(flags);
    s->last_result = rc;
    if (rc == PL_FRAME_OK) s->frame++;
    else if (rc == PL_FRAME_DROP) s->dropped++;
    return rc;
}
