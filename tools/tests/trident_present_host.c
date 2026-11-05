#include "tools/tests/trident_device_fake.h"
static struct trident_fake f;
static struct trident_device d;
static u8 vram[TRI_FAKE_REAL_BYTES], client[TRI_DISPLAY_BYTES + 4];
static struct trident_fake_event trace[1024];
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static int test(void)
{
    CHECK(!trident_fake_reset(&f, vram, sizeof(vram), trace, 1024));
    trident_fake_bind(&f, &d);
    f.resources.client = client;
    f.resources.client_bytes = TRI_DISPLAY_BYTES;
    d.verified_bytes = TRI_DISPLAY_BYTES;
    /* Stand-in for the separately tested Init and linear-mode sequences. */
    f.pc98_initialized = true; f.cr_color[0x21] = 0x20; f.trace_lfb = true;
    for (u32 i = 0; i < TRI_DISPLAY_BYTES; ++i) client[i] = (u8)(i * 17u + 1u);
    CHECK(!trident_present(&d, 0, 0, 640, 480));
    for (u32 i = 0; i < TRI_DISPLAY_BYTES; ++i) CHECK(vram[i] == client[i]);
    CHECK(f.trace_count == 480 && !f.trace_overflow);
    for (u32 i = 0; i < 480; ++i)
        CHECK(f.trace[i].offset == i * 640 && f.trace[i].count == 160 && f.trace[i].width == 4);
    for (u32 i = 0; i < TRI_DISPLAY_BYTES; ++i) vram[i] = 0;
    trident_fake_trace_reset(&f);
    CHECK(!trident_present(&d, -2, 478, 7, 8));
    for (u32 i = 0; i < TRI_DISPLAY_BYTES; ++i)
        CHECK(vram[i] == ((i >= 478u * 640 && i < 478u * 640 + 5) ||
                           (i >= 479u * 640 && i < 479u * 640 + 5) ? client[i] : 0));
    CHECK(!trident_present(&d, 639, 479, 20, 20));
    CHECK(vram[307199] == client[307199]);
    u32 operations = f.operations;
    CHECK(!trident_present(&d, (-2147483647 - 1), 0, 2147483647, 1));
    CHECK(!trident_present(&d, 0, 0, 0, 1));
    CHECK(f.operations == operations);
    f.resources.client_bytes = 307199;
    CHECK(trident_present(&d, 0, 0, 640, 480) == TRI_E_ACCESS && f.operations == operations);
    f.resources.client_bytes = TRI_DISPLAY_BYTES;
    d.verified_bytes = 307199;
    CHECK(trident_present(&d, 0, 0, 640, 480) == TRI_E_ACCESS && f.operations == operations);
    d.verified_bytes = TRI_DISPLAY_BYTES;
    f.resources.mapped_bytes = 307199;
    CHECK(trident_present(&d, 0, 0, 640, 480) == TRI_E_ACCESS && f.operations == operations);
    f.resources.mapped_bytes = TRI_BAR0_DECODE_BYTES;
    f.resources.decode_bytes = 307199;
    CHECK(trident_present(&d, 0, 0, 640, 480) == TRI_E_ACCESS && f.operations == operations);
    f.resources.decode_bytes = TRI_BAR0_DECODE_BYTES;
    trident_fake_fail(&f, operations + 1, -73);
    CHECK(trident_present(&d, 0, 0, 8, 1) == -73);
    trident_fake_fail(&f, 0, 0);
    f.resources.client = client + 1;
    CHECK(!trident_present(&d, 0, 0, 8, 1));
    for (u32 i = 0; i < 8; ++i) CHECK(vram[i] == client[i+1]);
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0));
    __builtin_unreachable();
}
