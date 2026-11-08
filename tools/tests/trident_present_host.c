#include "tools/tests/trident_device_fake.h"
static struct trident_fake f;
static struct trident_device d;
static u8 vram[TRI_FAKE_REAL_BYTES], client[256];
static struct trident_fake_event trace[512];
static u32 sync_calls;
static struct trident_device *sync_device;
static int sync_result;
int tri_sync(struct trident_device *device)
{
    ++sync_calls; sync_device = device; return sync_result;
}
#define CHECK(c) do { if (!(c)) return __LINE__; } while (0)
static int test(void)
{
    CHECK(!trident_fake_reset(&f, vram, sizeof(vram), trace, 512));
    trident_fake_bind(&f, &d);
    f.resources.client = client;
    f.resources.client_bytes = sizeof(client);
    f.pc98_initialized = true; f.cr_color[0x21] = 0x20; f.trace_lfb = true;
    d.declared_bytes = sizeof(vram);
    for (u32 i = 0; i < sizeof(client); ++i) client[i] = (u8)(i + 1);
    const int rects[][4] = {{0,0,5,3}, {-2,1,4,9}, {0,0,0,0},
        {(-2147483647-1),-1,2147483647,1}, {4,2,9,9}};
    const u8 cpps[] = {1,2,4};
    for (u32 backing = 0; backing < 2; ++backing)
      for (u32 mode = 0; mode < 3; ++mode) {
        u32 cpp = cpps[mode], pitch = 7 * cpp;
        f.resources.ram_fallback = backing;
        d.mode.client = (struct tri_surface){.bytes=pitch*3, .pitch=pitch,
            .width=5, .height=3, .cpp=cpp};
        d.mode.display = d.mode.client;
        d.mode.display.off = 32;
        for (u32 r = 0; r < 5; ++r) {
            for (u32 i = 0; i < 256; ++i) vram[i] = 0xAA;
            trident_fake_trace_reset(&f);
            u32 before = sync_calls;
            sync_result = 0;
            CHECK(!trident_present(&d, rects[r][0], rects[r][1], rects[r][2], rects[r][3]));
            CHECK(sync_calls == before + 1 && sync_device == &d);
            u32 expected = 0;
            for (u32 i = 0; i < 256; ++i) {
                bool copy = false;
                if (backing && i >= 32 && i < 32 + pitch * 3) {
                    int y = (i - 32) / pitch, x = ((i - 32) % pitch) / cpp;
                    copy = x < 5 && ((r == 0) || (r == 1 && y >= 1 && x < 2) ||
                                                     (r == 4 && y == 2 && x == 4));
                }
                CHECK(vram[i] == (copy ? client[i-32] : 0xAA));
                if (copy) ++expected;
            }
            CHECK(f.trace_count == expected && !f.trace_overflow);
            for (u32 i = 0; i < f.trace_count; ++i) CHECK(f.trace[i].width == 1);
        }
        sync_result = -73;
        trident_fake_trace_reset(&f);
        CHECK(trident_present(&d,0,0,5,3) == -73 && !f.trace_count);
        sync_result = 0;
        if (backing) {
            u32 limits[] = {d.mode.client.bytes, d.mode.display.bytes,
                f.resources.client_bytes, f.resources.mapped_bytes,
                f.resources.decode_bytes, d.declared_bytes};
            for (u32 i = 0; i < 6; ++i) {
                u32 *limit = i == 0 ? &d.mode.client.bytes : i == 1 ? &d.mode.display.bytes :
                    i == 2 ? &f.resources.client_bytes : i == 3 ? &f.resources.mapped_bytes :
                    i == 4 ? &f.resources.decode_bytes : &d.declared_bytes;
                *limit = 1;
                CHECK(trident_present(&d,4,2,1,1) == TRI_E_CAP_TOO_SMALL);
                *limit = limits[i];
            }
            trident_fake_trace_reset(&f);
            trident_fake_fail(&f, f.operations + 2, -79);
            CHECK(trident_present(&d,0,0,5,3) == -79);
            CHECK(f.trace_count == 2);
            trident_fake_fail(&f,0,0);
        }
      }
    /* Independent row oracle: every destination alignment and short tail,
     * with an unaligned RAM source on alternating rows. */
    for (u32 method = TRI_PRESENT_DWORD_SYNC; method <= TRI_PRESENT_DWORD_PLAIN; ++method) {
        d.present_method = (enum tri_present_method)method;
        f.resources.ram_fallback = true;
        d.mode.client = (struct tri_surface){.bytes=51, .pitch=17,
            .width=16, .height=3, .cpp=1};
        for (u32 alignment = 0; alignment < 4; ++alignment) {
            d.mode.display = d.mode.client;
            d.mode.display.off = 32 + alignment;
            d.mode.display.pitch = 19;
            d.mode.display.bytes = 57;
            for (u32 width = 0; width <= 16; ++width) {
                trident_fake_trace_reset(&f);
                CHECK(!trident_present(&d,0,0,width,3));
                u32 at = 0;
                for (u32 row = 0; width && row < 3; ++row) {
                    u32 dst = 32 + alignment + row * 19;
                    for (u32 i = 0; i < width;) {
                        u32 n = (dst + i) % 4 == 0 && width - i >= 4 ? 4 : 1;
                        CHECK(at < f.trace_count);
                        const struct trident_fake_event *e = &trace[at++];
                        CHECK(e->space == TRI_SPACE_LFB && e->write &&
                              e->offset == dst+i && e->width == n && !e->rc);
                        for (u32 b = 0; b < n; ++b)
                            CHECK(vram[dst+i+b] == client[row*17+i+b]);
                        if (n == 4 && method != TRI_PRESENT_DWORD_PLAIN) {
                            CHECK(at < f.trace_count);
                            e = &trace[at++];
                            CHECK(e->width == 1 && !e->rc);
                            if (method == TRI_PRESENT_DWORD_SYNC)
                                CHECK(e->space == TRI_SPACE_REGS && !e->write && e->offset == 0x3DA);
                            else
                                CHECK(e->space == TRI_FAKE_PORT_SPACE && e->write &&
                                      e->offset == 0x5F && e->value == 0);
                        }
                        i += n;
                    }
                }
                CHECK(at == f.trace_count && !f.trace_overflow);
            }
        }
        /* The dword write and its following read/wait propagate failure. */
        d.mode.display.off = 32;
        for (u32 fault = 1; fault <= (method == TRI_PRESENT_DWORD_PLAIN ? 1u : 2u); ++fault) {
            trident_fake_trace_reset(&f);
            trident_fake_fail(&f, f.operations + fault, -79);
            CHECK(trident_present(&d,0,0,4,1) == -79 && f.trace_count == fault);
            trident_fake_fail(&f,0,0);
        }
    }
    sync_result = TRI_E_INVAL;
    CHECK(trident_present(0,0,0,1,1) == TRI_E_INVAL);
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? 1 : 0) : "memory");
    __builtin_unreachable();
}
