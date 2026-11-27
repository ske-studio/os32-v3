#include "gfx2_int.h"

struct gfx2_surf gfx2_surfs[GFX2_MAX_SURFACES];
static struct gfx2_adapter adapter;
static u8 screen[32 * 32], pixels[32 * 32];
static unsigned calls, palette_calls;
static int fail;
static struct gfx2_rect sent;

int appslot_gui_present_ok(void) { return 1; }
struct gfx2_adapter *gfx2_adapter_get(u32 id) { return id ? NULL : &adapter; }
int gfx2_caller_get(struct gfx2_caller *c) { c->app_id = 1; return 0; }
i32 gfx_screen_owner(void) { return 1; }

int gfx2_sync_get(const struct gfx2_adapter *a, u32 head, GfxSyncCtx **ctx, u32 *mode)
{
    *ctx = (GfxSyncCtx *)1;
    *mode = GFXSYNC_SHADOW;
    return 0;
}

int gfxsync_write_rows(GfxSyncCtx *ctx, u32 x, u32 y, u32 w, u32 h,
                       const u8 *src, u32 pitch)
{
    calls++;
    sent = (struct gfx2_rect){x, y, w, h};
    if (fail) return OS32_ERR_IO;
    for (u32 j = 0; j < h; j++)
        for (u32 i = 0; i < w; i++)
            screen[(y+j)*32+x+i] = src[j*pitch+i];
    return 0;
}

static int palette(void *ctx, u32 first, u32 count, const u32 *rgb)
{
    palette_calls++;
    return 0;
}

static int partial(int center)
{
    struct gfx2_surf *s = gfx2_surfs;
    int w = center ? 3 : 5, h = center ? 1 : 6;
    int cut_h = center ? 1 : 2;
    s->dirty = (struct gfx2_rect){2, 3, w, h};
    calls = 0;
    for (unsigned i = 0; i < sizeof(screen); i++) screen[i] = 0;
    if (gfx2_present(0, 0, center ? 3 : 2, 3, center ? 1 : w, cut_h)) return 1;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int hit = y >= 3 && y < 3 + cut_h &&
                      (center ? x == 3 : x >= 2 && x < 2 + w);
            if (screen[y*32+x] != (hit ? pixels[y*32+x] : 0)) return 1;
        }
    if (gfx2_present(0, 0, 0, 0, 32, 32) || calls != 2 ||
        sent.x != 2 || sent.y != (center ? 3 : 5) ||
        sent.w != w || sent.h != (center ? 1 : 4)) return 1;
    for (int y = 0; y < 32; y++)
        for (int x = 0; x < 32; x++) {
            int hit = x >= 2 && x < 2+w && y >= 3 && y < 3+h;
            if (screen[y*32+x] != (hit ? pixels[y*32+x] : 0)) return 1;
        }
    return gfx2_present(0, 0, 0, 0, 32, 32) || calls != 2;
}

static int run(void)
{
    static const GfxBackendV2 be = { .palette = palette };
    struct gfx2_surf *s = gfx2_surfs;
    u32 rgb[7] = {1};
    adapter.be = &be;
    adapter.heads = 1;
    adapter.flags = GFX2_AF_ACTIVE;
    s->state = GFX2_ST_IDLE;
    s->flags = GFX2_SF_ALIAS;
    s->role = GFX2_ROLE_DISPLAY;
    s->w = s->h = s->pitch = 32;
    s->fmt = GFX2_FMT_INDEX8;
    s->kva = pixels;
    for (unsigned i = 0; i < sizeof(pixels); i++) pixels[i] = i % 251 + 1;
    if (gfx2_palette(0,250,7,rgb) != OS32_ERR_INVAL ||
        gfx2_palette(0,0,0,rgb) != OS32_ERR_INVAL || palette_calls) return 1;
    s->dirty = (struct gfx2_rect){2,3,5,6};
    fail = 1;
    if (gfx2_present(0,0,0,0,32,32) != OS32_ERR_IO) return 2;
    fail = 0;
    if (gfx2_present(0,0,0,0,32,32) || calls != 2 ||
        sent.x != 2 || sent.y != 3 || sent.w != 5 || sent.h != 6) return 2;
    int middle = partial(1), rows = partial(0);
    return middle && rows ? 3 : middle ? 4 : rows ? 5 : 0;
}

void _start(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(run()));
    __builtin_unreachable();
}
