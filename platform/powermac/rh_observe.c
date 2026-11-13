/* OF PowerPC binding 2.1 section 6 (virtual client addresses, I/G IO);
 * NetBSD macppc/dev/ofb.c:195-202 (firmware display address). No mode setting.
 * translate is the OF MMU method (phys.lo mode true, bottom to top).
 * State is owned here: no firmware access from bind/stage/phy. */
#include "rh_observe.h"
#include "ofw.h"
#include "kstring.h"
static struct pm_rh_fb fb, full_fb;
static u32 fb_va, fb_mmu;
static struct pm_rh_colors colors;
static volatile u8 *pixels;
static int output;
static u32 reached;
static unsigned last_stage;
static const u8 digits[16][7] = {
    {14,17,19,21,25,17,14}, {4,12,4,4,4,4,14},
    {14,17,1,2,4,8,31}, {30,1,1,14,1,1,30},
    {2,6,10,18,31,2,2}, {31,16,16,30,1,1,30},
    {14,16,16,30,17,17,14}, {31,1,2,4,8,8,8},
    {14,17,17,14,17,17,14}, {14,17,17,15,1,1,14},
    {14,17,17,31,17,17,17}, {30,17,17,30,17,17,30},
    {15,16,16,16,16,16,15}, {30,17,17,17,17,17,30},
    {31,16,16,30,16,16,31}, {31,16,16,30,16,16,16}
};
static int prop(int node, const char *name, u32 *out)
{
    u8 b[4];
    if (ofw_getprop(node, name, b, sizeof(b)) != 4) return 0;
    *out = ((u32)b[0]<<24) | ((u32)b[1]<<16) | ((u32)b[2]<<8) | b[3];
    return 1;
}
static int translate(int mmu, u32 va, u32 *pa)
{
    cell a[3] = {va, 0, 0};
    if (ofw_call_method("translate", mmu, 1, 3, a) || !a[2]) return 0;
    *pa = a[0];
    return 1;
}
int pm_rh_capture(int stdout_ih)
{
    u32 va, mmu, pa;
    int chosen = ofw_finddevice("/chosen");
    cell node[2] = {(cell)stdout_ih, 0};
    char type[16];
    fb = (struct pm_rh_fb){0};
    full_fb = (struct pm_rh_fb){0};
    colors = (struct pm_rh_colors){0};
    pixels = 0;
    output = stdout_ih;
    reached = 0;
    last_stage = 0;
    if (ofw_call("instance-to-package", 1, 1, node) ||
        (node[1] == 0 || node[1] == (cell)-1)) return 0;
    int n = ofw_getprop(node[1], "device_type", type, sizeof(type));
    if (n < 7 || n > 8 || (n == 8 && type[7]) || kstrncmp(type, "display", 7)) return 0;
    if (!prop(node[1], "address", &va) ||
        !prop(node[1], "width", &fb.width) ||
        !prop(node[1], "height", &fb.height) ||
        !prop(node[1], "linebytes", &fb.stride) ||
        !prop(node[1], "depth", &fb.depth)) return -1;
    if ((fb.depth != 8 && fb.depth != 16 && fb.depth != 32) || fb.width < 320 ||
        fb.width > ~0U / (fb.depth / 8) ||
        fb.stride < fb.width * (fb.depth / 8) || !fb.stride ||
        !prop(chosen, "mmu", &mmu)) return -1;
    u32 offset = va & (PM_RH_FB_PAGE - 1);
    u32 rows = (PM_RH_FB_LIMIT - offset) / fb.stride;
    if (rows > PM_RH_FB_ROWS) rows = PM_RH_FB_ROWS;
    if (rows > fb.height) rows = fb.height;
    if (rows < 32) return -1;
    u32 bytes = rows * fb.stride;
    u32 rounded = (offset + bytes + PM_RH_FB_PAGE - 1) & ~(PM_RH_FB_PAGE - 1);
    if (rounded > PM_RH_FB_LIMIT || va > ~0U - (rounded - offset) ||
        !translate(mmu, va, &pa) || (pa & (PM_RH_FB_PAGE - 1)) != offset ||
        pa > ~0U - (rounded - offset)) return -1;
    u32 base = pa - offset;
    /* Resolve every mapped page, including both partial boundary pages. */
    for (u32 off = 0; off < rounded; off += PM_RH_FB_PAGE) {
        u32 next;
        if (!translate(mmu, va - offset + off, &next) || next != base + off)
            return -1;
    }
    fb_va = va;
    fb_mmu = mmu;
    fb.pa = pa;
    fb.bytes = bytes;
    /* height is the captured firmware height; rendering uses bytes/stride. */
    return 1;
}
const struct pm_rh_fb *pm_rh_framebuffer(void) { return &fb; }
/* IEEE 1275 display color@: (index -- r g b). Wrapper returns bottom first.
 * Sum compares brightness without assuming a firmware palette ordering. */
static void capture_colors(void)
{
    cell lo[3] = {0}, hi[3] = {255};
    colors = (struct pm_rh_colors){.rc = PM_RH_COLOR_FAILED};
    if (ofw_call_method("color@", output, 1, 3, lo) ||
        ofw_call_method("color@", output, 1, 3, hi)) {
        /* IEEE 1275 display color!: (r g b index --). Set only the
         * terminal endpoints while firmware calls are still available. */
        cell black[4] = {0, 0, 0, 0}, white[4] = {255, 255, 255, 255};
        if (ofw_call_method("color!", output, 4, 0, black) ||
            ofw_call_method("color!", output, 4, 0, white)) return;
        colors = (struct pm_rh_colors){.rc = PM_RH_COLOR_WRITTEN,
            .foreground = 255, .background = 0};
        return;
    }
    u32 a = lo[0] + lo[1] + lo[2], b = hi[0] + hi[1] + hi[2];
    if (a == b) { colors.rc = PM_RH_COLOR_EQUAL; return; }
    colors = (struct pm_rh_colors){.rc = PM_RH_COLOR_OK,
        .foreground = a > b ? 0 : 255, .background = a > b ? 255 : 0};
}
const struct pm_rh_colors *pm_rh_full_colors(void) { return &colors; }
int pm_rh_capture_full(void)
{
    colors = (struct pm_rh_colors){0};
    full_fb = (struct pm_rh_fb){0};
    if (!fb.bytes) return PM_RH_FULL_ABSENT;
    u32 offset = fb_va & (PM_RH_FB_PAGE - 1);
    if (fb.height > ~0U / fb.stride) return PM_RH_FULL_SIZE;
    u32 bytes = fb.stride * fb.height;
    if (bytes > PM_RH_FB_FULL_LIMIT - offset) return PM_RH_FULL_SIZE;
    u32 rounded = (offset + bytes + PM_RH_FB_PAGE - 1) & ~(PM_RH_FB_PAGE - 1);
    u32 base = fb.pa - offset;
    if (rounded > PM_RH_FB_FULL_LIMIT || fb_va - offset > ~0U - rounded ||
        base > ~0U - rounded) return PM_RH_FULL_SIZE;
    for (u32 off = 0; off < rounded; off += PM_RH_FB_PAGE) {
        u32 next;
        if (!translate(fb_mmu, fb_va - offset + off, &next) || next != base + off)
            return PM_RH_FULL_TRANSLATE;
    }
    if (fb.depth == 8) capture_colors();
    full_fb = fb;
    full_fb.bytes = bytes;
    return PM_RH_FULL_OK;
}
void pm_rh_discard_full(void)
{ full_fb = (struct pm_rh_fb){0}; colors = (struct pm_rh_colors){0}; }
const struct pm_rh_fb *pm_rh_full_framebuffer(void) { return &full_fb; }

int pm_rh_of_mark(unsigned stage)
{
    if (stage > 15) return -1;
    if (!output) {
        u32 ih;
        if (!prop(ofw_finddevice("/chosen"), "stdout", &ih)) return -1;
        output = ih ? (int)ih : ofw_open(PM_SCREEN);
        if (output == -1) return -1;
    }
    char text[128];
    unsigned pos = 0;
    text[pos++] = '\r'; text[pos++] = '\n';
    for (unsigned y = 0; y < 7; y++) {
        for (unsigned x = 0; x < 5; x++) {
            char c = digits[stage][y] & (16U >> x) ? '#' : ' ';
            text[pos++] = c; text[pos++] = c;
        }
        text[pos++] = '\r'; text[pos++] = '\n';
    }
    return ofw_write(output, text, pos) == (int)pos ? 0 : -1;
}
/* Only the arch/platform alias is converted, never a raw firmware address. */
void pm_rh_bind(u32 fb_va) { pixels = fb.bytes && fb_va ? (volatile u8 *)fb_va : 0; }
u32 pm_rh_reached(void) { return reached; }
unsigned pm_rh_last_stage(void) { return last_stage; }
static void pixel(u32 x, u32 y, int ink, int error)
{
    if (x >= fb.width || y >= fb.bytes / fb.stride) return;
    u32 off = y * fb.stride + x * (fb.depth / 8);
    /* White/black are independent of RGB555/RGB565; red flags errors. */
    if (fb.depth == 8) {
        /* Apple OF CLUT: 0 white, 0xff black; 0x23 is red in the Mac system CLUT. */
        pixels[off] = ink ? 0xff : error ? 0x23 : 0;
    } else if (fb.depth == 16) {
        pixels[off] = ink ? 0xff : error ? 0x7c : 0;
        pixels[off+1] = ink ? 0xff : 0;
    } else {
        pixels[off] = 0;
        pixels[off+1] = ink || error ? 0xff : 0;
        pixels[off+2] = ink ? 0xff : 0;
        pixels[off+3] = ink ? 0xff : 0;
    }
}
static void glyph(u32 x, u32 y, unsigned value, u32 scale, int error)
{
    for (u32 row = 0; row < 7 * scale; row++)
        for (u32 col = 0; col < 6 * scale; col++)
            pixel(x+col, y+row, col < 5 * scale &&
                (digits[value & 15][row/scale] & (16U >> (col/scale))), error);
}
void pm_rh_stage(unsigned stage)
{
    if (stage > 15 || ((reached & (1U << stage)) && last_stage == stage)) return;
    reached |= 1U << stage;
    last_stage = stage;
    if (!pixels) return;
    u32 cell = fb.width / 16;
    u32 scale = cell / 6;
    if (scale > 4) scale = 4;
    u32 vertical = (fb.bytes / fb.stride / 2) / 7;
    if (scale > vertical) scale = vertical;
    if (reached == (1U << stage))
        for (u32 off = 0; off < fb.bytes; off++) pixels[off] = 0;
    for (unsigned i = 0; i < 16; i++)
        if (reached & (1U << i)) glyph(i * cell, 0, i, scale, i >= PM_RH_ESCC_PREPARE);
    ppc_sync();
}
void pm_rh_phy(u32 port, u32 addr, u32 id1, u32 id2)
{
    if (!pixels) return;
    u32 words[4] = {port, addr, id1, id2};
    /* Fixed second page inside the same band; stage history stays visible. */
    u32 rows = fb.bytes / fb.stride;
    u32 stage_scale = rows / 2 / 7;
    if (stage_scale > 4) stage_scale = 4;
    u32 y = stage_scale * 7 + 2;
    u32 scale = fb.width / (35 * 6);
    if (scale > 2) scale = 2;
    if (scale > (rows - y) / 7) scale = (rows - y) / 7;
    for (u32 i = 0; i < 4; i++)
        for (u32 j = 0; j < 8; j++)
            glyph((i*9+j)*6*scale, y, words[i] >> (28-j*4), scale, 0);
    ppc_sync();
}
