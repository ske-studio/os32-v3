#include "hwdev/gf/internal.h"
int gf_display_blank(struct gf_display_context *, int);

struct event { u32 write, bar, offset, width, value; };
static struct event events[8];
static u32 count, fail_at, seq;
static int failures;
#define CHECK(x) do { if (!(x)) failures++; } while (0)

static int record(u32 write, u32 bar, u32 offset, u32 width, u32 value)
{
    u32 n = count++;
    if (n < 8) events[n] = (struct event){write, bar, offset, width, value};
    return count == fail_at ? -1 : 0;
}
static int fake_read(void *opaque, u32 bar, u32 offset, u32 width, u32 *out)
{
    (void)opaque;
    int rc = record  (0, bar, offset, width, seq);
    if (!rc) *out = seq;
    return rc;
}
static int fake_write(void *opaque, u32 bar, u32 offset, u32 width, u32 value)
{
    (void)opaque;
    return record(1, bar, offset, width, value);
}
static void run(void)
{
    struct gf_display_context ctx = {0};
    ctx.io.read = fake_read; ctx.io.write = fake_write; ctx.reusable = true;
    CHECK(gf_display_blank(0, 1) == OS32_ERR_INVAL);
    CHECK(gf_display_blank(&ctx, -1) == OS32_ERR_INVAL);
    CHECK(gf_display_blank(&ctx, 2) == OS32_ERR_INVAL);
    CHECK(gf_display_blank(&ctx, 0) == 0 && count == 0);
    for (int blank = 1; blank >= 0; blank--) {
        const u32 offsets[] = {0xc03c4,0xc03c5,0xc03c4,0xc03c5,0xc03c4,0xc03c5,0xc03c4,0xc03c5};
        u32 values[] = {1,0,0,1,1,0,0,3};
        seq = blank ? 0x95 : 0xb5;
        values[1] = seq; values[5] = blank ? 0xb5 : 0x95;
        count = 0;
        CHECK(gf_display_blank(&ctx, blank) == 0 && count == 8 && ctx.blank == (bool)blank);
        for (u32 i = 0; i < 8; i++) {
            CHECK(events[i].write == (i != 1) && events[i].bar == 0 &&
                  events[i].offset == offsets[i] && events[i].width == 1 && events[i].value == values[i]);
        }
        count = 0;
        CHECK(gf_display_blank(&ctx, blank) == 0 && count == 0);
    }
    for (u32 n = 1; n <= 8; n++) {
        count = 0; fail_at = n; ctx.blank = false; ctx.reusable = true;
        CHECK(gf_display_blank(&ctx, 1) == -1 && count == n && !ctx.blank && !ctx.reusable);
    }
}
void _start(void)
{
    run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(failures ? 1 : 0));
    __builtin_unreachable();
}
