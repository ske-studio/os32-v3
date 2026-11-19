#include "gfx_dirty.h"
#include "gfx_hal.h"
#include "gfx_present_contract.h"
#include "gfx2_bridge_fake.h"

DirtyRectQueue dirty_queue, prev_dirty;
GfxCounters gfx_counters;
int gfx_current_height = 400;
static u8 pixel;
u8 *bb_b = &pixel;
static int allowed = 1, width_queries, transfers, planar_calls, raster_calls;
static int diagnostics;
static GFX_Rect sent[8];

int display_provider_v1_width(void) { width_queries++; return 800; }
int display_provider_present_ok(void) { return allowed; }
void gfx_diag_first_present(u32 before) { (void)before; diagnostics++; }
void gfx_present_diag(void) { diagnostics++; }
void gfx_planar_present_dirty(int sync) { (void)sync; planar_calls++; }
void gfx_planar_present_raster(GFX_RasterPalTable *table) { (void)table; raster_calls++; }

static void transfer(int x, int y, int w, int h)
{
    if (transfers < 8) sent[transfers] = (GFX_Rect){x, y, w, h};
    transfers++;
    gfx_counters.commits++;
}

static GfxBackend backend = {.bb_format = GFX_BB_PACKED8, .present_rect = transfer};
GfxBackend *g_backend = &backend;

static int check_blocked(GFX_RasterPalTable *table)
{
    int before = width_queries;
    dirty_queue.count = 1;
    gfx_add_dirty_rect(224, 200, 32, 1);
    gfx_present_dirty();
    gfx_present_nosync();
    gfx_present();
    gfx_present_rect(2, 3, 4, 5);
    gfx_present_raster(table);
    return dirty_queue.count == 1 && transfers == 0 && planar_calls == 0 &&
           raster_calls == 0 && width_queries == before;
}

int main(void)
{
    GFX_RasterPalTable table = {.count = 1};
    bb_b = 0;
    if (!check_blocked(&table)) return 2;
    bb_b = &pixel;
#ifdef TEST_POWERMAC
    int before = diagnostics;
    allowed = 0;
    if (!check_blocked(&table) || diagnostics != before) return 2;
    allowed = 1;
#endif
    dirty_queue.count = 0;
    prev_dirty.count = 1;
    gfx_add_dirty_rect(0, 1, 32, 1);
    gfx_add_dirty_rect(TEST_WIDTH - 1, 20, 1, 2);
    gfx_present_dirty();
    if (transfers != 2 || sent[0].x || sent[0].y != 1 || sent[0].w != 32 ||
        sent[0].h != 1 || sent[1].x != TEST_WIDTH - 32 || sent[1].w != 32 ||
        sent[1].y != 20 || sent[1].h != 2 || gfx_counters.commits != 1 ||
        dirty_queue.count || prev_dirty.count || planar_calls) return 1;
    gfx_present();
    if (transfers != 3 || sent[2].w != TEST_WIDTH || sent[2].h != gfx_current_height) return 1;
    gfx_add_dirty_rect(32, 0, 1, 1);
    gfx_present_raster(0);
    table.count = 0;
    gfx_present_raster(&table);
    table.count = GFX_RASTER_MAX_ENTRIES + 1;
    gfx_present_raster(&table);
    if (dirty_queue.count != 1 || transfers != 3 || raster_calls) return 3;
    table.count = 1;
    gfx_present_raster(&table);
    if (dirty_queue.count || transfers != 4 || raster_calls) return 3;
    backend.bb_format = GFX_BB_PLANAR4;
    gfx_present_raster(&table);
    return raster_calls == 1 && transfers == 4 ? 0 : 3;
}
