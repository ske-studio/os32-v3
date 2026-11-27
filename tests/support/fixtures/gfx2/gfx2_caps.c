#include "gfx2_int.h"
#include "kstring.h"
static struct gfx2_adapter adapter;
static GfxBackend v1;
static int fail_caps;
static int sync_bound;
static u32 sync_mode;
int gfx2_sync_get(const struct gfx2_adapter *a, u32 head, GfxSyncCtx **ctx, u32 *mode)
{
    (void)a;
    if (head || !sync_bound) return OS32_ERR_NOTFOUND;
    *ctx = (GfxSyncCtx *)&adapter;
    *mode = sync_mode;
    return 0;
}
void gfxsync_profile(const GfxSyncCtx *ctx, char out[16])
{
    (void)ctx;
    out[0] = 'p'; out[1] = 'c'; out[2] = 0;
}
static int backend_caps(void *ctx, GFX2_Caps *out)
{
    (void)ctx;
    *out = (GFX2_Caps){0}; out->max_w = 987;
    return fail_caps;
}
static const GfxBackendV2 backend = { .caps = backend_caps };
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i == 0 ? &adapter : 0; }
void *kmemcpy(void *d, const void *s, u32 n)
{
    u8 *dst = d; const u8 *src = s;
    while (n--) *dst++ = *src++;
    return d;
}
#define EXPECT(x) do { if (!(x)) return __LINE__; } while (0)
static int probe(void)
{
    u8 data[304]; GFX2_Caps c;
    GfxMode mode = { .width = 1024, .height = 768 };
    adapter.v1 = &v1; adapter.kind = GFX2_KIND_TRIDENT; adapter.index = 2;
    v1.selected_mode = &mode;
    EXPECT(gfx2_caps_from_v1(&adapter, &c) == 0);
    EXPECT(c.max_w == 1024 && c.max_h == 768 && c.adapter == 2);
    EXPECT(c.caps2d_sw == 2046 && c.exact == (2046 & ~(8 | 256)));
    EXPECT(c.fmt_scanout == ((1 << GFX2_FMT_INDEX8) | (1 << GFX2_FMT_RGB565) | (1 << GFX2_FMT_XRGB8888)));
    adapter.kind = GFX2_KIND_PC98;
    EXPECT(gfx2_caps_from_v1(&adapter, &c) == 0 && c.fmt_scanout == 0);
    adapter.be = &backend; adapter.caps.max_w = 42; fail_caps = 1;
    gfx2_caps_refresh(&adapter); EXPECT(adapter.caps.max_w == 42);
    fail_caps = 0; gfx2_caps_refresh(&adapter); EXPECT(adapter.caps.max_w == 987);
    for (u32 i = 0; i < sizeof(data); i++) data[i] = 0xa5;
    EXPECT(gfx2_caps(0, (GFX2_Caps *)data, 3) == OS32_ERR_INVAL && data[0] == 0xa5);
    EXPECT(gfx2_caps(1, (GFX2_Caps *)data, 8) == OS32_ERR_INVAL && data[0] == 0xa5);
    EXPECT(gfx2_caps(0, 0, 8) == OS32_ERR_INVAL);
    EXPECT(gfx2_caps(0, (GFX2_Caps *)data, 8) == 0 && data[8] == 0xa5);
    EXPECT(gfx2_caps(0, (GFX2_Caps *)data, 300) == 0 && data[255] != 0xa5 && data[256] == 0xa5);
    adapter.caps.cpu_direct = 7; adapter.caps.sync_mode = 7;
    adapter.caps.sync_profile[0] = 'x';
    EXPECT(gfx2_caps(0, &c, sizeof(c)) == 0);
    EXPECT(c.cpu_direct == 0 && c.sync_mode == 0 && c.sync_profile[0] == 0);
    sync_bound = 1; sync_mode = GFXSYNC_SHADOW;
    EXPECT(gfx2_caps(0, &c, sizeof(c)) == 0);
    EXPECT(c.cpu_direct == 1 && c.sync_mode == GFXSYNC_SHADOW && c.sync_profile[0] == 'p');
    sync_mode = GFXSYNC_DIRECT;
    EXPECT(gfx2_caps(0, &c, sizeof(c)) == 0 && c.sync_mode == GFXSYNC_DIRECT);
    sync_bound = 0;
    EXPECT(gfx2_caps(0, &c, sizeof(c)) == 0 && c.cpu_direct == 0 && c.sync_profile[0] == 0);
    return 0;
}
void _start(void) { int rc = probe(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
