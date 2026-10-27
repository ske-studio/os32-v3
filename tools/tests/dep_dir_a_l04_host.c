/* Real platform forwarding with isolated driver substitutes (ILP32). */
#include "platform_device.h"
#include "loop_dev.h"

static void die(int rc) __attribute__((noreturn));
static void die(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(c) do { if (!(c)) die(1); } while (0)
static int calls, result, expected_slot;
static u16 cyl, bps;
static u8 head, sect, buffer[16];
static u32 total;
static int status_bps;
static const char path[] = "/image.nhd";
static u16 *out_c, *out_b;
static u8 *out_h, *out_s;
static u32 *out_t;
static int *out_status_b;

int loop_dev_get_geometry(int slot, u16 *c, u8 *h, u8 *s, u16 *b, u32 *t)
{
    CHECK(slot == expected_slot && c == out_c && h == out_h && s == out_s);
    CHECK(b == out_b && t == out_t);
    calls++;
    if (c) *c = 991;
    if (h) *h = 7;
    if (s) *s = 31;
    if (b) *b = 512;
    if (t) *t = 123456;
    return result;
}
int loop_dev_read_chs(int slot, u16 c, u8 h, u8 s, void *buf)
{
    CHECK(slot == expected_slot && c == 0x9123 && h == 0x87 && s == 0x65);
    CHECK(buf == buffer);
    calls++;
    buffer[0] = 0x39;
    return result;
}
int loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf)
{
    CHECK(slot == expected_slot && lba == 0x87654321 && count == 0x12345678);
    CHECK(buf == buffer);
    calls++;
    buffer[0] = 0x57;
    return result;
}
int loop_dev_attach(const char *p, int slot)
{
    CHECK(p == path && slot == expected_slot);
    calls++;
    return result;
}
void loop_dev_detach(int slot)
{
    CHECK(slot == expected_slot);
    calls++;
}
int loop_dev_status(int slot, u32 *t, int *b)
{
    CHECK(slot == expected_slot && t == out_t && b == out_status_b);
    calls++;
    if (t) *t = 654321;
    if (b) *b = 256;
    return result;
}
static void run(void)
{
    /* Forward errors, absent/present and success without interpreting them. */
    for (result = -4; result <= 1; result++) {
        for (expected_slot = -1; expected_slot <= 3; expected_slot++) {
            calls = 0;
            CHECK(platform_loop_dev_attach(path, expected_slot) == result);
            CHECK(platform_loop_dev_read_chs(expected_slot, 0x9123, 0x87, 0x65,
                                              buffer) == result);
            CHECK(buffer[0] == 0x39);
            CHECK(platform_loop_dev_read_lba(expected_slot, 0x87654321, 0x12345678,
                                              buffer) == result);
            CHECK(buffer[0] == 0x57);
            platform_loop_dev_detach(expected_slot);
            CHECK(calls == 4);
            /* Every combination, including individually NULL output pointers. */
            for (unsigned int mask = 0; mask < 32; mask++) {
                out_c = mask & 1 ? &cyl : 0;
                out_h = mask & 2 ? &head : 0;
                out_s = mask & 4 ? &sect : 0;
                out_b = mask & 8 ? &bps : 0;
                out_t = mask & 16 ? &total : 0;
                CHECK(platform_loop_dev_get_geometry(expected_slot, out_c, out_h,
                          out_s, out_b, out_t) == result);
                CHECK(!out_c || cyl == 991);
                CHECK(!out_h || head == 7);
                CHECK(!out_s || sect == 31);
                CHECK(!out_b || bps == 512);
                CHECK(!out_t || total == 123456);
            }
            for (unsigned int mask = 0; mask < 4; mask++) {
                out_t = mask & 1 ? &total : 0;
                out_status_b = mask & 2 ? &status_bps : 0;
                CHECK(platform_loop_dev_status(expected_slot, out_t, out_status_b) == result);
                CHECK(!out_t || total == 654321);
                CHECK(!out_status_b || status_bps == 256);
            }
            CHECK(calls == 40);
        }
    }
}
void _start(void)
{
    run();
    die(0);
}
