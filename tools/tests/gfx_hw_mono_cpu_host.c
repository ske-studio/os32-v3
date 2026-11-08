#include "gfx_hal.h"
#include "gfx_hw_mono_cpu.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), "d"(sizeof(msg)-1) : "memory"); finish(1); } } while (0)

static u8 screen[100 * 100], bits[13 * 100];
static int calls, fail_at, last_key;
static int put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key)
{
    calls++; last_key = key;
    CHECK(w * h <= 4096 && pitch == (u32)w);
    if (calls == fail_at) return -37;
    for (int j = 0; j < h; j++) for (int i = 0; i < w; i++)
        if (src[j * pitch + i] != key) screen[(y+j)*100+x+i] = src[j*pitch+i];
    return 0;
}
static GfxBackend backend = { .put = put };
GfxBackend *g_backend = &backend;

void _start(void)
{
    for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++) {
        screen[y*100+x] = 99;
        if ((x + 2*y) % 3 == 0) bits[y*13+x/8] |= 0x80U >> (x%8);
    }
    CHECK(gfx_hw_mono_cpu(0, 0, 100, 100, bits, 13, 8, -1, 3, 2, 91, 93) == 0);
    CHECK(calls == 3 && last_key == 9);
    for (int y = 0; y < 100; y++) for (int x = 0; x < 100; x++)
        CHECK(screen[y*100+x] == ((x >= 3 && x < 94 && y >= 2 && y < 95 && (x+2*y)%3 == 0) ? 8 : 99));
    calls = 0;
    CHECK(gfx_hw_mono_cpu(0, 0, 100, 100, bits, 13, 8, 7, 7, 4, 17, 5) == 0);
    CHECK(calls == 1 && last_key == -1);
    for (int y = 4; y < 9; y++) for (int x = 7; x < 24; x++)
        CHECK(screen[y*100+x] == ((x+2*y)%3 == 0 ? 8 : 7));
    calls = 0; fail_at = 2;
    CHECK(gfx_hw_mono_cpu(0, 0, 100, 100, bits, 13, 8, 7, 0, 0, 100, 100) == -37 && calls == 2);
    calls = 0; fail_at = 0;
    CHECK(gfx_hw_mono_cpu(0, 0, 100, 100, bits, 13, 8, 7, 110, 0, 1, 1) == 0 && calls == 0);
    finish(0);
}
