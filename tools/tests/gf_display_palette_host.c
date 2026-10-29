#include "../../hwdev/gf/display/palette.c"

struct event { u32 bar, off, width, value; };
static struct event events[VIDEO_PALETTE_BYTES + 2];
static u32 calls, fail_at;
static struct gf_display_context ctx;
static u8 input[VIDEO_PALETTE_BYTES];

static int fake_write(void *opaque, u32 bar, u32 off, u32 width, u32 value)
{
    (void)opaque;
    calls++;
    if (calls == fail_at) return -1;
    events[calls - 1] = (struct event){bar, off, width, value};
    return 0;
}

static int test(void)
{
    ctx.io.write = fake_write;
    for (u32 i = 0; i < VIDEO_PALETTE_BYTES; i++) {
        ctx.palette[i] = 0xa5;
        input[i] = (u8)(i * 13U);
    }
    if (gf_display_palette(0, 0, 1, input) != OS32_ERR_INVAL ||
        gf_display_palette(&ctx, 0, 1, 0) != OS32_ERR_INVAL ||
        gf_display_palette(&ctx, 256, 1, input) != OS32_ERR_INVAL ||
        gf_display_palette(&ctx, 0, 0, input) != OS32_ERR_INVAL ||
        gf_display_palette(&ctx, 255, 2, input) != OS32_ERR_INVAL ||
        gf_display_palette(&ctx, 1, ~0U, input) != OS32_ERR_INVAL || calls)
        return 1;
    if (gf_display_palette(&ctx, 254, 2, input) || calls != 8 ||
        events[0].off != GF_MODE_DAC_MASK || events[0].value != GF_MODE_BYTE_MASK ||
        events[1].off != GF_MODE_DAC_INDEX || events[1].value != 254)
        return 2;
    for (u32 i = 0; i < calls; i++)
        if (events[i].bar != GF_MODE_REGISTER_BAR || events[i].width != 1 ||
            (i >= 2 && (events[i].off != GF_MODE_DAC_DATA || events[i].value != input[i-2])))
            return 3;
    for (u32 i = 0; i < VIDEO_PALETTE_BYTES; i++)
        if (ctx.palette[i] != (i < 254 * VIDEO_RGB_COMPONENTS ? 0xa5 : input[i - 254 * VIDEO_RGB_COMPONENTS]))
            return 4;
    calls = 0;
    if (gf_display_palette(&ctx, 0, VIDEO_PALETTE_ENTRIES, input) ||
        calls != VIDEO_PALETTE_BYTES + 2 || events[1].value != 0)
        return 5;
    for (u32 i = 0; i < VIDEO_PALETTE_BYTES; i++)
        if (ctx.palette[i] != input[i] || events[i+2].value != input[i] ||
            events[i+2].off != GF_MODE_DAC_DATA) return 6;
    for (u32 failure = 1; failure <= 5; failure++) {
        calls = 0; fail_at = failure; ctx.last_rc = 0;
        if (gf_display_palette(&ctx, 3, 1, input) != -1 || calls != failure ||
            ctx.last_rc != -1) return 7;
    }
    return 0;
}

void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (rc) : "memory");
    __builtin_unreachable();
}
