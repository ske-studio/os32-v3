#include "../../hwdev/gf/foundation.h"
struct event { u32 write, bar, off, width, value; };
static struct event events[7];
static unsigned count, fail_at;
static u32 amount;
static int access_io(u32 write, u32 bar, u32 off, u32 width, u32 *value)
{
    unsigned n = count++;
    if (n == fail_at) return -1;
    if (n >= 7) return -1;
    if (!write) *value = off == 0x200 ? 0x17111113U : amount;
    events[n] = (struct event){write, bar, off, width, *value};
    return 0;
}
static int rd(void *p, u32 bar, u32 off, u32 width, u32 *v)
{ (void)p; return access_io  (0, bar, off, width, v); }
static int wr(void *p, u32 bar, u32 off, u32 width, u32 v)
{ (void)p; return access_io(1, bar, off, width, &v); }
static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { __asm__ volatile("":::"memory"); if (!(x)) fail(__LINE__); } while (0)
void _start(void)
{
    const u32 offsets[] = {0x10020c, 0x200, 0x1700, 0x1704, 0x1708, 0x170c, 0x200};
    const u32 values[] = {0x12345678, 0xffffffff, 0x12345678, 0, 0, 0x12345678, 0x17111113};
    struct gf_foundation f = {0};
    f.io.read = rd; f.io.write = wr; amount = values[0];
    for (unsigned pass = 0; pass < 2; pass++) {
        count = 0; fail_at = 99; f.expected_pmc = 42;
        CHECK(gf_mc_init(&f) == GF1_OK && count == 7);
        CHECK(f.expected_pmc == 0x17111113U);
        for (unsigned i = 0; i < 7; i++) {
            CHECK(events[i].write == (i != 0 && i != 6) && events[i].bar == 0);
            CHECK(events[i].off == offsets[i] && events[i].width == 4);
            CHECK(events[i].value == values[i]);
        }
    }
    for (unsigned i = 0; i < 7; i++) {
        count = 0; fail_at = i; f.expected_pmc = 42;
        CHECK(gf_mc_init(&f) == GF1_EIO && count == i + 1);
        CHECK(f.expected_pmc == 42U);
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0));
    for (;;) {}
}
