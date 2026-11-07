#include "tools/tests/trident_device_fake.h"
static struct trident_fake f;
static struct trident_device d;
static u8 vram[TRI_FAKE_REAL_BYTES], client[64];
static struct trident_fake_event trace[64];
static u32 sync_calls;
static struct trident_device *sync_device;
static int sync_result;
/* F1 owns the GE wait oracle. This seam verifies F7 delegates exactly once,
 * including NoAccel and errors, without touching CLIENT or DISPLAY. */
int tri_sync(struct trident_device *device)
{
    ++sync_calls;
    sync_device = device;
    return sync_result;
}
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static int test(void)
{
    CHECK(!trident_fake_reset(&f, vram, sizeof(vram), trace, 64));
    trident_fake_bind(&f, &d);
    f.resources.client = client;
    f.resources.client_bytes = sizeof(client);
    f.pc98_initialized = true;
    f.cr_color[0x21] = 0x20;
    f.trace_lfb = true;
    for (u32 i = 0; i < sizeof(client); ++i) client[i] = (u8)(i + 1);
    const int rects[][4] = {{0,0,640,480}, {-2,478,7,8}, {0,0,0,0},
        {(-2147483647-1),-1,2147483647,1}};
    const u8 bpp[] = {8,16,24,32};
    for (u32 mode = 0; mode < 4; ++mode)
        for (u32 no_accel = 0; no_accel < 2; ++no_accel) {
            d.mode.selected.bpp = bpp[mode];
            d.mode.selected.display_width = 1024;
            d.settings.no_accel = no_accel;
            d.accel.initialized = !no_accel;
            for (u32 r = 0; r < 4; ++r) {
                u32 before = sync_calls;
                sync_result = r == 1 ? -73 : TRI_OK;
                CHECK(trident_present(&d, rects[r][0], rects[r][1], rects[r][2], rects[r][3]) == sync_result);
                CHECK(sync_calls == before + 1 && sync_device == &d);
                CHECK(!f.operations && !f.trace_count && !f.trace_overflow);
                for (u32 i = 0; i < sizeof(client); ++i) {
                    CHECK(client[i] == (u8)(i + 1));
                    CHECK(vram[i] == 0);
                }
            }
        }
    sync_result = TRI_E_INVAL;
    u32 before = sync_calls;
    CHECK(trident_present(0, 0, 0, 1, 1) == TRI_E_INVAL);
    CHECK(sync_calls == before + 1 && !sync_device);
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
