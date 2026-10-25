/* Real internal headers under ILP32; no libc or hardware access. */
#include "gfx_hal.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg) - 1) : "memory"); \
    finish(1); \
} } while (0)

STATIC_ASSERT(sizeof(GfxMode) == 12, mode_size);
STATIC_ASSERT(sizeof(u32) == 4, ilp32);

static const GfxMode modes[] = {
    {640, 480, GFX_FMT_PACKED8, 8, 0, 0, 640},
    {640, 200, GFX_FMT_PACKED8, 8, 0, 0, 640}, /* Not legacy planar. */
    {640, 400, GFX_FMT_PLANAR4, 4, 0, 0, 80},
    {640, 200, GFX_FMT_PLANAR4, 4, 1, 0, 80}
};

void _start(void)
{
    GfxFormatDesc d;
    u32 row, plane, total;
    CHECK(gfx_format_desc(GFX_FMT_PLANAR4, &d));
    CHECK(d.bpp == 4 && d.indexed == 1 && d.planes == 4 &&
          d.bits_per_plane == 1 && d.palette_count == 16);
    CHECK(gfx_format_desc(GFX_FMT_PACKED8, &d));
    CHECK(d.bpp == 8 && d.indexed == 1 && d.planes == 1 &&
          d.bits_per_plane == 8 && d.palette_count == 256);
    CHECK(!gfx_format_desc(255, &d));
    const u32 widths[] = {1, 7, 8, 9, 639, 640, 641, U32_MAX};
    const u32 rows[] = {1, 1, 1, 2, 80, 80, 81, 536870912};
    for (u32 i = 0; i < sizeof(widths) / sizeof(widths[0]); ++i) {
        CHECK(gfx_format_row(GFX_FMT_PLANAR4, widths[i], &row));
        CHECK(row == rows[i]);
        CHECK(gfx_format_row(GFX_FMT_PACKED8, widths[i], &row));
        CHECK(row == widths[i]);
    }
    CHECK(!gfx_format_row(GFX_FMT_PLANAR4, 0, &row));
    CHECK(!gfx_format_row(GFX_FMT_PACKED8, 0, &row));
    CHECK(!gfx_format_row(255, 640, &row));
    CHECK(gfx_mode_layout(&modes[0], &plane, &total));
    CHECK(plane == 307200 && total == 307200);
    CHECK(gfx_mode_layout(&modes[2], &plane, &total));
    CHECK(plane == 32000 && total == 128000);
    CHECK(gfx_mode_layout(&modes[3], &plane, &total));
    CHECK(plane == 16000 && total == 64000);
    GfxMode m = {9, 1, GFX_FMT_PLANAR4, 4, 0, 0, 2};
    CHECK(gfx_mode_layout(&m, &plane, &total) && plane == 2 && total == 8);
    m.pitch = 1;
    CHECK(!gfx_mode_layout(&m, &plane, &total));
    m = modes[0]; m.pitch = 672; /* Padded pitch is retained. */
    CHECK(gfx_mode_layout(&m, &plane, &total) && total == 322560);
    m.width = 0; CHECK(!gfx_mode_layout(&m, &plane, &total));
    m = modes[0]; m.height = 0; CHECK(!gfx_mode_layout(&m, &plane, &total));
    m = modes[0]; m.format = 255; CHECK(!gfx_mode_layout(&m, &plane, &total));
    CHECK(!gfx_mode_layout(NULL, &plane, &total));
    m = modes[0]; m.height = 1; m.pitch = U32_MAX;
    CHECK(gfx_mode_layout(&m, &plane, &total) && total == U32_MAX);
    m.height = 2; m.pitch = U32_MAX / 2;
    CHECK(gfx_mode_layout(&m, &plane, &total) && total == 4294967294UL);
    m.pitch++; CHECK(!gfx_mode_layout(&m, &plane, &total));
    m = modes[2]; m.height = 1; m.pitch = U32_MAX / 4;
    CHECK(gfx_mode_layout(&m, &plane, &total) && total == 4294967292UL);
    m.pitch++; CHECK(!gfx_mode_layout(&m, &plane, &total));

    GfxBackend be = {.modes = modes, .mode_count = 4, .default_mode = 2};
    CHECK(gfx_backend_mode(&be) == &modes[2]);
    CHECK(gfx_mode_pick(&be, 0) == &modes[2]);
    CHECK(gfx_mode_pick(&be, 1) == &modes[3]);
    be.selected_mode = &modes[3];
    CHECK(gfx_backend_mode(&be) == &modes[3]);
    CHECK(gfx_mode_pick(&be, 0) == &modes[2]);
    be.selected_mode = NULL; be.mode_count = 2; be.default_mode = 0;
    CHECK(gfx_mode_pick(&be, 1) == &modes[0]); /* Packed stays at default. */
    be.default_mode = 2; CHECK(!gfx_mode_pick(&be, 0));
    be.mode_count = 0; CHECK(!gfx_backend_mode(&be));
    be.mode_count = 4; be.default_mode = 0; be.modes = NULL;
    CHECK(!gfx_mode_pick(&be, 1));
    CHECK(!gfx_backend_mode(NULL));
    CHECK(!gfx_mode_pick(NULL, 0));
    finish(0);
}
