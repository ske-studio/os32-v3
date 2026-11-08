#include "../gfx_direct_internal.h"

static GFX_Cmd q[GFX_CMD_MAX];
static u32 nq;
#define GFX_CMD_ARENA_BYTES 4096u
static u8 arena[GFX_CMD_ARENA_BYTES];
static u32 used;

static int rect_valid(int x, int y, int w, int h)
{
    return x >= -32768 && x <= 32767 && y >= -32768 && y <= 32767 &&
           w > 0 && w <= 32767 && h > 0 && h <= 32767 &&
           x + w - 1 <= 32767 && y + h - 1 <= 32767;
}

static int span_valid(const void *data, u32 pitch, u32 rowbytes, int h)
{
    u32 tail;
    if (!data || pitch < rowbytes || (u32)(h - 1) > (0xffffffffU - rowbytes) / pitch)
        return 0;
    tail = (u32)(h - 1) * pitch + rowbytes;
    return (u32)data <= 0xffffffffU - (tail - 1);
}

void gfx_cmd_drop(void)
{
    nq = 0;
    used = 0;
}

int gfx_flush(void)
{
    u32 done = 0;
    int rc;
    if (!nq) return 0;
    for (u32 i = 0; i < nq; i++) {
        if (q[i].op == GFX_CMD_PUT || q[i].op == GFX_CMD_MONO)
            q[i].data = (u32)(arena + q[i].data);
    }
    rc = gfx_api->gfx_hw_submit(q, nq, gfx_gen, &done);
    gfx_cmd_drop();
    if (rc) gfx_lost = 1;
    return rc;
}

static int reserve_command(void)
{
    if (!gfx_direct) return OS32_ERR_NOSYS;
    if (gfx_lost) return OS32_ERR_STALE;
    if (nq == GFX_CMD_MAX) return gfx_flush();
    return 0;
}

/* Intersect both coordinates with the screen, preserving their offset. */
static int clip_axis(int *dst, int *src, int *length, int limit)
{
    long long start = 0, end = *length;
    long long d = *dst, s = *src;
    if (-d > start) start = -d;
    if (-s > start) start = -s;
    if ((long long)limit - d < end) end = (long long)limit - d;
    if ((long long)limit - s < end) end = (long long)limit - s;
    if (start >= end) return 0;
    *dst = (int)(d + start);
    *src = (int)(s + start);
    *length = (int)(end - start);
    return 1;
}

int gfx_cmd_enqueue_fill(int x, int y, int w, int h, u8 color, u8 rop)
{
    int rc, sx = x, sy = y;
    if (w <= 0 || h <= 0 || rop > 1) return OS32_ERR_INVAL;
    if (!clip_axis(&x, &sx, &w, gfx_fb.width) ||
        !clip_axis(&y, &sy, &h, gfx_fb.height)) return 0;
    if (!rect_valid(x, y, w, h)) return OS32_ERR_INVAL;
    rc = reserve_command();
    if (rc) return rc;
    q[nq++] = (GFX_Cmd){ .op = GFX_CMD_FILL, .rop = rop,
        .x = x, .y = y, .w = w, .h = h, .color = color };
    return 0;
}

int gfx_cmd_enqueue_copy(int dx, int dy, int sx, int sy, int w, int h)
{
    int rc;
    if (w <= 0 || h <= 0) return OS32_ERR_INVAL;
    if (!clip_axis(&dx, &sx, &w, gfx_fb.width) ||
        !clip_axis(&dy, &sy, &h, gfx_fb.height)) return 0;
    if (!rect_valid(dx, dy, w, h) || !rect_valid(sx, sy, w, h))
        return OS32_ERR_INVAL;
    rc = reserve_command();
    if (rc) return rc;
    q[nq++] = (GFX_Cmd){ .op = GFX_CMD_COPY, .x = dx, .y = dy,
        .w = w, .h = h, .sx = sx, .sy = sy };
    return 0;
}

/* MONO source offsets address the original bitmap, including a partial byte.
 * Every output row starts at bit 7 and has zeroed padding bits. */
static int push_rows(GFX_Cmd cmd, const u8 *src, u32 pitch, int bx, int by)
{
    u32 rowbytes = cmd.op == GFX_CMD_PUT ? (u32)cmd.w : ((u32)cmd.w + 7) / 8;
    int left = cmd.h;
    while (left > 0) {
        int rc = reserve_command();
        u32 rows;
        if (rc) return rc;
        rows = (GFX_CMD_ARENA_BYTES - used) / rowbytes;
        if (!rows) {
            rc = gfx_flush();
            if (rc) return rc;
            continue;
        }
        if (rows > (u32)left) rows = (u32)left;
        for (u32 r = 0; r < rows; r++) {
            const u8 *in = src + ((u32)by + r) * pitch;
            u8 *out = arena + used + r * rowbytes;
            for (u32 b = 0; b < rowbytes; b++) out[b] = 0;
            if (cmd.op == GFX_CMD_PUT) {
                for (u32 b = 0; b < rowbytes; b++) out[b] = in[b];
            } else {
                for (int b = 0; b < cmd.w; b++) {
                    u32 bit = (u32)(bx + b);
                    if (in[bit / 8] & (0x80U >> (bit % 8)))
                        out[b / 8] |= 0x80U >> (b % 8);
                }
            }
        }
        cmd.h = rows;
        cmd.data = used;
        cmd.pitch = rowbytes;
        q[nq++] = cmd;
        used += rows * rowbytes;
        by += rows;
        cmd.y += rows;
        left -= rows;
    }
    return 0;
}

int gfx_cmd_enqueue_put(int x, int y, int w, int h, const u8 *src, u32 pitch, int key)
{
    int left, top, right, bottom;
    if (!rect_valid(x, y, w, h) || key < -1 || key > 255 ||
        !span_valid(src, pitch, w, h)) return OS32_ERR_INVAL;
    left = x > 0 ? x : 0;
    top = y > 0 ? y : 0;
    right = x + w < gfx_fb.width ? x + w : gfx_fb.width;
    bottom = y + h < gfx_fb.height ? y + h : gfx_fb.height;
    if (left >= right || top >= bottom) return 0;
    if (right - left > 640) return OS32_ERR_INVAL;
    src += (u32)(top - y) * pitch + (left - x);
    return push_rows((GFX_Cmd){ .op = GFX_CMD_PUT, .x = left, .y = top,
        .w = right - left, .h = bottom - top, .key = key }, src, pitch, 0, 0);
}

int gfx_cmd_enqueue_mono(int x, int y, int w, int h, const u8 *bits, u32 pitch,
                         u8 fg, int bg, int cx, int cy, int cw, int ch)
{
    int left, top, right, bottom;
    if (!rect_valid(x, y, w, h) || !rect_valid(cx, cy, cw, ch) ||
        bg < -1 || bg > 255 || !span_valid(bits, pitch, ((u32)w + 7) / 8, h))
        return OS32_ERR_INVAL;
    left = x > cx ? x : cx;
    top = y > cy ? y : cy;
    right = x + w < cx + cw ? x + w : cx + cw;
    bottom = y + h < cy + ch ? y + h : cy + ch;
    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > gfx_fb.width) right = gfx_fb.width;
    if (bottom > gfx_fb.height) bottom = gfx_fb.height;
    if (left >= right || top >= bottom) return 0;
    if (right - left > 640) return OS32_ERR_INVAL;
    return push_rows((GFX_Cmd){ .op = GFX_CMD_MONO, .x = left, .y = top,
        .w = right - left, .h = bottom - top, .color = fg, .key = bg },
        bits, pitch, left - x, top - y);
}

int gfx_cmd_get(int x, int y, int w, int h, u8 *dst, u32 pitch)
{
    int rc;
    if (!rect_valid(x, y, w, h) || !span_valid(dst, pitch, w, h))
        return OS32_ERR_INVAL;
    if (gfx_lost) return OS32_ERR_STALE;
    rc = gfx_flush();
    if (rc) return rc;
    rc = gfx_api->gfx_hw_get(x, y, w, h, dst, pitch, gfx_gen);
    if (rc) gfx_lost = 1;
    return rc;
}
