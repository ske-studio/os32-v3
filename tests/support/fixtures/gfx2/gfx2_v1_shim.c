/* C16 only: real tables, fake C10/C11 and v1 hooks, no libc. */
#include "gfx2_int.h"

static struct gfx2_adapter adapter;
static GfxBackend v1;
static struct gfx2_surf surf;
static u32 pixels[8], seen_head, seen_arg;
static int calls;
#define EXPECT(x) do { if (!(x)) return __LINE__; } while (0)
int gfx2_caps_from_v1(struct gfx2_adapter *a, GFX2_Caps *out)
{
    if (a != &adapter) return OS32_ERR_INVAL;
    out->max_w = 123;
    return 7;
}
int gfx2_v1_enum_head(const GfxBackend *be, u32 h, GFX2_HeadInfo *out)
{
    (void)out;
    return be == &v1 && h == 1 ? 11 : OS32_ERR_INVAL;
}
int gfx2_v1_enum_conn(const GfxBackend *be, u32 c, GFX2_ConnectorInfo *out)
{
    (void)out;
    return be == &v1 && c == 2 ? 12 : OS32_ERR_INVAL;
}
int gfx2_v1_enum_mode(const GfxBackend *be, u32 h, u32 f, u32 i, GFX2_ModeInfo *out)
{
    (void)out;
    return be == &v1 && h == 1 && f == 3 && i == 4 ? 13 : OS32_ERR_INVAL;
}
static int mode_a(u32 id) { ++calls; return id == 99 ? 21 : -1; }
static int mode_b(u32 id) { ++calls; return id == 99 ? 22 : -1; }
static int flip(u8 h, const struct GfxScanout *s, int sync, u32 dl)
{
    ++calls;
    return h == 1 && s->reservation_id == 19 && s->offset == 64 &&
        s->bytes == 32 && s->pitch == 16 && s->width == 4 && s->height == 2 &&
        s->format == GFX2_FMT_ARGB8888 && sync == (int)seen_arg && dl == 345 ? 23 : -1;
}
static int vsync(u8 h, u32 after, u32 dl, u32 *seen)
{
    ++calls; *seen = 456;
    return h == 1 && after == 123 && dl == 345 ? 24 : -1;
}
static void palette(int first, int count, const u8 *rgb)
{
    ++calls;
    seen_arg = first == 254 && count == 2 && rgb[0] == 1 && rgb[1] == 8 &&
        rgb[2] == 15 && rgb[3] == 0 && rgb[4] == 15 && rgb[5] == 0;
}
static int gamma(u8 h, const u16 rgb[3][256])
{
    ++calls;
    return h == 1 && rgb[0][0] == 17 && rgb[1][0] == 18 && rgb[2][255] == 19 ? 25 : -1;
}
static int image(u8 h, const struct GfxCursorImage *im)
{
    ++calls;
    return h == 1 && im->argb_pm == pixels && im->bytes == 32 && im->pitch == 16 &&
        im->width == 4 && im->height == 2 && !im->hot_x && !im->hot_y ? 26 : -1;
}
static int move(u8 h, i32 x, i32 y)
{
    ++calls; return h == 1 && x == -123 && y == 456 ? 27 : -1;
}
static int show(u8 h, int s) { ++calls; return h == 1 && s == 1 ? 28 : -1; }
static int dpms(u8 h, u32 s) { ++calls; return h == 1 && s == 3 ? 29 : -1; }

static int probe(void)
{
    const GfxBackendV2 *b = &gfx2_v1_shim;
    void *c = &adapter;
    GFX2_Caps caps = {0};
    GFX2_ModeInfo mode = {.id = 99};
    u32 done = 0, fail = 111, base = 1, size = 2, phys = 3;
    u8 *va = (u8 *)pixels;
    u16 rgb[768] = {0};
    const u32 pal[] = {0x001080ff, 0x0000ff00};
    adapter.v1 = &v1;
    adapter.fence.issued = 0xfffffff0u;
    surf = (struct gfx2_surf){.priv = {19}, .gpu_off = 64, .bytes = 32,
        .pitch = 16, .w = 4, .h = 2, .fmt = GFX2_FMT_ARGB8888, .kva = (u8 *)pixels};
    EXPECT(b->abi == GFX_HAL2_ABI && gfx2_lost_be.abi == GFX_HAL2_ABI);
    EXPECT(b->caps(c, &caps) == 7 && caps.max_w == 123);
    EXPECT(b->enum_head(c, 1, 0) == 11);
    EXPECT(b->enum_conn(c, 2, 0) == 12);
    EXPECT(b->enum_mode(c, 1, 3, 4, 0) == 13);
    EXPECT(b->heap_range(c, &base, &size, &phys) == 0 && !base && !size && !phys);
    EXPECT(b->fence_state(c, &done, &fail) == 0 && done == 0xfffffff0u && !fail);
    EXPECT(b->check(c, 0) == OS32_ERR_NOSYS);
    EXPECT(b->emit(c, 0, 99) == OS32_ERR_NOSYS);
    EXPECT(b->readback(c, 0, 0) == OS32_ERR_NOSYS);
    EXPECT(b->alloc(c, &surf) == OS32_ERR_NOSPC);
    EXPECT(b->release(c, &surf) == 0);
    EXPECT(b->kva(c, &surf, &va) == OS32_ERR_NOSYS && va == (u8 *)pixels);
    EXPECT(b->gart_bind(c, 0, 1, &base) == OS32_ERR_NOSYS);
    EXPECT(b->gart_unbind(c, 0, 1) == 0);
    EXPECT(b->kick(c) == 0 && b->stop(c) == 0);
    EXPECT(b->fence_emit(c, 99, 345) == 0 && b->fence_wait(c, 99, 345) == 0);
    EXPECT(b->edid(c, 0, 0, 0) == OS32_ERR_NOSYS);
    EXPECT(b->flip_done(c, 1, &seen_head) == OS32_ERR_NOSYS);
    EXPECT(b->mode_set(c, 0, 1, &mode, &surf) == OS32_ERR_NOSYS);
    EXPECT(b->flip(c, 1, &surf, 0, 345) == OS32_ERR_NOSYS);
    EXPECT(b->vblank_wait(c, 1, 123, 345, &done) == OS32_ERR_NOSYS);
    EXPECT(b->palette(c, 254, 2, pal) == OS32_ERR_NOSYS);
    EXPECT(b->gamma(c, 1, rgb) == OS32_ERR_NOSYS);
    EXPECT(b->cursor_image(c, 1, &surf) == OS32_ERR_NOSYS);
    EXPECT(b->cursor_move(c, 1, -123, 456) == OS32_ERR_NOSYS);
    EXPECT(b->cursor_show(c, 1, 1) == OS32_ERR_NOSYS);
    EXPECT(b->dpms(c, 1, 3) == OS32_ERR_NOSYS && calls == 0);
    v1.set_mode = mode_a;
    EXPECT(b->mode_set(c, 0, 1, &mode, &surf) == 21);
    v1.set_mode = mode_b;
    EXPECT(b->mode_set(c, 0, 1, &mode, &surf) == 22);
    EXPECT(b->mode_set(c, 1, 1, &mode, &surf) == OS32_ERR_NOSYS && calls == 2);
    v1.page_flip = flip;
    seen_arg = 0;
    EXPECT(b->flip(c, 1, &surf, 0, 345) == 23);
    seen_arg = 1;
    EXPECT(b->flip(c, 1, &surf, GFX2_FLIP_VBLANK, 345) == 23);
    v1.vsync = vsync;
    EXPECT(b->vblank_wait(c, 1, 123, 345, &done) == 24 && done == 456);
    v1.set_palette = palette;
    EXPECT(b->palette(c, 254, 2, pal) == 0 && seen_arg == 1);
    rgb[0] = 17; rgb[256] = 18; rgb[767] = 19;
    v1.gamma = gamma;
    EXPECT(b->gamma(c, 1, rgb) == 25);
    v1.cursor_image = image;
    EXPECT(b->cursor_image(c, 1, &surf) == 26);
    v1.cursor_move = move; v1.cursor_show = show; v1.dpms = dpms;
    EXPECT(b->cursor_move(c, 1, -123, 456) == 27);
    EXPECT(b->cursor_show(c, 1, 1) == 28);
    EXPECT(b->dpms(c, 1, 3) == 29);
    /* Detached table must work without any v1/module pointer. */
    adapter.v1 = 0;
    calls = 0;
    b = &gfx2_lost_be;
    EXPECT(b->caps(c, &caps) == OS32_ERR_STALE);
    EXPECT(b->check(c, 0) == OS32_ERR_STALE);
    EXPECT(b->heap_range(c, &base, &size, &phys) == OS32_ERR_STALE);
    EXPECT(b->alloc(c, &surf) == OS32_ERR_STALE);
    EXPECT(b->kva(c, &surf, &va) == OS32_ERR_STALE);
    EXPECT(b->gart_bind(c, 0, 1, &base) == OS32_ERR_STALE);
    EXPECT(b->emit(c, 0, 99) == OS32_ERR_STALE);
    EXPECT(b->readback(c, 0, 0) == OS32_ERR_STALE);
    EXPECT(b->fence_emit(c, 99, 345) == OS32_ERR_STALE);
    EXPECT(b->fence_wait(c, 99, 345) == OS32_ERR_STALE);
    EXPECT(b->enum_head(c, 1, 0) == OS32_ERR_STALE);
    EXPECT(b->enum_conn(c, 2, 0) == OS32_ERR_STALE);
    EXPECT(b->enum_mode(c, 1, 3, 4, 0) == OS32_ERR_STALE);
    EXPECT(b->edid(c, 0, 0, 0) == OS32_ERR_STALE);
    EXPECT(b->mode_set(c, 0, 1, &mode, &surf) == OS32_ERR_STALE);
    EXPECT(b->flip(c, 1, &surf, 0, 345) == OS32_ERR_STALE);
    EXPECT(b->flip_done(c, 1, &done) == OS32_ERR_STALE);
    EXPECT(b->vblank_wait(c, 1, 123, 345, &done) == OS32_ERR_STALE);
    EXPECT(b->palette(c, 254, 2, pal) == OS32_ERR_STALE);
    EXPECT(b->gamma(c, 1, rgb) == OS32_ERR_STALE);
    EXPECT(b->cursor_image(c, 1, &surf) == OS32_ERR_STALE);
    EXPECT(b->cursor_move(c, 1, -123, 456) == OS32_ERR_STALE);
    EXPECT(b->cursor_show(c, 1, 1) == OS32_ERR_STALE);
    EXPECT(b->dpms(c, 1, 3) == OS32_ERR_STALE);
    EXPECT(b->fence_state(c, &done, &fail) == 0 && done == adapter.fence.issued && fail == done);
    EXPECT(b->release(c, &surf) == 0 && b->gart_unbind(c, 0, 1) == 0);
    EXPECT(b->kick(c) == 0 && b->stop(c) == 0 && calls == 0);
    return 0;
}
void _start(void)
{
    int rc = probe();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) {}
}
