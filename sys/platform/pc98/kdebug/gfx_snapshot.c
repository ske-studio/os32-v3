/* Per-flip pixel evidence is an optional, separate replay. Read the drawing
 * bank immediately before a planar flip, while its aperture still selects it. */
#include "gfx_measure_wire.h"
#include "sys/hal/gfx/gfx.h"
#include "sys/kern/exec/surface_query.h"
#include "sys/kern/mm/pgalloc.h"
#include "lib/serial_diag.h"
#include "generated/kdebug_import_macros.h"

struct image {
    struct image *next;
    u32 pages, event, pixels;
    u8 data[]; /* scan followed by full CLIENT in logical row order */
};
static struct image *head, **tail = &head;

struct plane { volatile const u8 *p; u32 pitch; };

static int planes(const struct surface_query_source *source, struct plane *out,
                  u32 count, u32 rowbytes, u32 height, u32 format)
{
    if (source->count != 1 && source->count != count) return -1;
    for (u32 i = 0; i < count; i++) {
        struct surface_ref ref = source->refs[source->count == 1 ? 0 : i];
        if (ref.sid >= LEDGER_MAX_SURFACES) return -1;
        const struct ledger_surface *s = &ledger_surfaces[ref.sid];
        u32 plane = source->count == 1 ? i : 0;
        if (ref.generation != s->gen || s->closing || s->format != format ||
            s->pitch < rowbytes || height > s->height || plane >= s->planes ||
            !s->npages || s->npages > U32_MAX / PAGE_SIZE) return -1;
        u32 bytes = s->npages * PAGE_SIZE, off = s->plane_offset[plane];
        if (off > bytes || height > (bytes - off) / s->pitch) return -1;
        out[i].pitch = s->pitch;
        out[i].p = (volatile const u8 *)P2V_IO(s->first * PAGE_SIZE) + off;
    }
    return 0;
}

static void unpack(u8 *out, const struct plane *p, u32 count,
                   u32 width, u32 height, u32 scroll)
{
    for (u32 y = 0; y < height; y++) {
        u32 row = (y + scroll) % height;
        if (count == 1) {
            for (u32 x = 0; x < width; x++) *out++ = p->p[row * p->pitch + x];
        } else {
            for (u32 x = 0; x < width; x += 8) {
                u8 bits[4];
                for (u32 i = 0; i < 4; i++) bits[i] = p[i].p[row * p[i].pitch + x / 8];
                for (u32 b = 0; b < 8 && x + b < width; b++) {
                    u8 value = 0;
                    for (u32 i = 0; i < 4; i++) value |= ((bits[i] >> (7-b)) & 1U) << i;
                    *out++ = value;
                }
            }
        }
    }
}

int gfx_snapshot_take(u32 event, const struct gfx_trace_event *e)
{
    struct surface_query_source client, display;
    struct plane scan[4], expected[4];
    if (e->w <= 0 || e->w > 640 || e->h <= 0 || e->h > 480 ||
        e->y < 0 || e->y >= e->h ||
        gfx_surface_source(LEDGER_ROLE_CLIENT, &client) ||
        gfx_surface_source(LEDGER_ROLE_DISPLAY, &display)) return -1;
    u32 count = display.count;
    if (count != 1 && count != 4) return -1;
    u32 format = count == 4 ? GFX_FMT_PLANAR4 : GFX_FMT_PACKED8;
    u32 rowbytes = count == 4 ? (e->w + 7) / 8 : e->w;
    if (planes(&display, scan, count, rowbytes, e->h, format) ||
        planes(&client, expected, count, rowbytes, e->h, format)) return -1;
    u32 pixels = (u32)e->w * e->h;
    u32 pages = PAGE_ALIGN_UP(sizeof(struct image) + 2 * pixels) / PAGE_SIZE;
    u32 phys = pgalloc_alloc_phys(LEDGER_OWNER_KERNEL, pages);
    if (!phys) return -1;
    struct image *image = P2V(phys);
    *image = (struct image){.pages = pages, .event = event, .pixels = pixels};
    unpack(image->data, scan, count, e->w, e->h, e->y);
    unpack(image->data + pixels, expected, count, e->w, e->h, 0);
    *tail = image;
    tail = &image->next;
    return 0;
}

static void emit_pixels(const u8 *p, u32 n)
{
    /* Each run is a hex count (1..255) and one palette index. */
    while (n) {
        u32 run = 1;
        while (run < n && run < 255 && p[run] == *p) run++;
        serial_diag_hex(run, 2);
        serial_diag_hex(*p, 2);
        p += run;
        n -= run;
    }
}

void gfx_snapshot_emit(u32 event)
{
    if (!head || head->event != event) return;
    serial_puts_polled("GXT1 pixels ");
    emit_pixels(head->data, head->pixels);
    serial_puts_polled(" ");
    emit_pixels(head->data + head->pixels, head->pixels);
    serial_puts_polled("\r\n");
    struct image *old = head;
    head = head->next;
    if (!head) tail = &head;
    pgalloc_free_n_owner(LEDGER_OWNER_KERNEL, V2P(old) / PAGE_SIZE, old->pages);
}
