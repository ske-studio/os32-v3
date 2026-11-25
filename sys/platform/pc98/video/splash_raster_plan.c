/* Validate and prepare every row before any VRAM transfer. */
#include "splash_raster_play.h"
#include "pc98.h"
#include "palette.h"

int pl_v2_prepare(const struct OsriImage *image, struct PlV2Plan *plan)
{
    u16 current[OSRI_COLORS];
    u32 pos = 0;
    if (!image || !plan || (image->version != OSRI_VERSION_1 && image->version != OSRI_VERSION_2) ||
        (image->band != 1 && image->band != 2 && image->band != 4) ||
        (image->version == OSRI_VERSION_1 &&
         (image->count > OSRI_V1_MAX_EVENTS || (!image->count && image->band != 4))) ||
        image->count > OSRI_IMAGE_EVENTS) return -1;
    for (u32 i = 0; i < OSRI_COLORS; i++) {
        if (image->pal[i] > 0xfff) return -1;
        current[i] = image->pal[i];
    }
    plan->max_outs = plan->total_outs = 0;
    plan->row[0] = (struct PlV2Row){0};
    for (u32 y = 1; y < SPLASH_RASTER_VISIBLE_LINES; y++) {
        struct PlV2Row *r = &plan->row[y];
        u16 seen = 0;
        *r = (struct PlV2Row){0};
        while (pos < image->count && image->event[pos].line <= y) {
            const struct OsriEvent *e = &image->event[pos++];
            if (e->line != y || e->line % image->band || e->index >= OSRI_COLORS || e->rgb444 > 0xfff ||
                e->reserved0 || e->reserved1 || (seen & (1u << e->index))) return -1;
            if (image->version == OSRI_VERSION_1 &&
                ((seen && e->index <= image->event[pos - 2].index) ||
                 current[e->index] == e->rgb444)) return -1;
            seen |= 1u << e->index;
            u16 old = current[e->index], rgb = e->rgb444;
            current[e->index] = rgb;
            /* v2 same-color events preserve file order but emit nothing. */
            if (old == rgb) continue;
            u32 outs = ((old & 0x0f0) != (rgb & 0x0f0)) +
                       ((old & 0xf00) != (rgb & 0xf00)) +
                       ((old & 0x00f) != (rgb & 0x00f));
            if (r->count + outs + !!r->count > OSRI_DEFAULT_OUT_BUDGET ||
                r->count + outs + !!r->count > sizeof(r->port)) return -1;
            if (!r->count) r->first = e->index;
            else {
                r->port[r->count] = PAL_IDX_PORT;
                r->value[r->count++] = e->index;
            }
            if ((old & 0x0f0) != (rgb & 0x0f0)) {
                r->port[r->count] = PAL_G_PORT;
                r->value[r->count++] = (rgb >> 4) & GFX_PALETTE_COMPONENT_MASK;
            }
            if ((old & 0xf00) != (rgb & 0xf00)) {
                r->port[r->count] = PAL_R_PORT;
                r->value[r->count++] = (rgb >> 8) & GFX_PALETTE_COMPONENT_MASK;
            }
            if ((old & 0x00f) != (rgb & 0x00f)) {
                r->port[r->count] = PAL_B_PORT;
                r->value[r->count++] = rgb & GFX_PALETTE_COMPONENT_MASK;
            }
        }
        if (r->count > plan->max_outs) plan->max_outs = r->count;
        plan->total_outs += r->count;
    }
    return pos == image->count ? 0 : -1;
}

