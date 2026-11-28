/* Two short rows in the four discontiguous display planes. */
#include "sys/platform/pc98/kdebug/gfx_snapshot.c"
static struct ledger_surface surfaces[5];
static u8 backing[5][PAGE_SIZE] __attribute__((aligned(4096)));
static u8 storage[PAGE_SIZE] __attribute__((aligned(4096)));
static int allocated, released;
struct ledger_surface *kdi_ptr_ledger_surfaces(void) { return surfaces; }
int gfx_surface_source(u32 role, struct surface_query_source *out)
{
    out->count = role == LEDGER_ROLE_DISPLAY ? 4 : 1;
    for (u32 p = 0; p < out->count; p++)
        out->refs[p] = (struct surface_ref){role == LEDGER_ROLE_CLIENT ? 0 : p + 1, 1};
    return 0;
}
void serial_puts_polled(const char *s) { (void)s; }
u32 pgalloc_alloc_phys(u32 owner, int n) { allocated++; return (u32)(uptr)storage; }
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n) { released++; return 1; }
void _start(void)
{
    for (u32 i = 0; i < 5; i++) {
        surfaces[i] = (struct ledger_surface){.first = (u32)(uptr)backing[i] / PAGE_SIZE,
            .npages = 1, .width = 8, .height = 2, .pitch = 1, .gen = 1,
            .planes = i ? 1 : 4, .format = GFX_FMT_PLANAR4, .plane_offset = {0,2,4,6}};
    }
    backing[1][1] = 0x80;
    backing[2][1] = 0x40;
    backing[3][1] = 0x20;
    backing[4][1] = 0x10;
    backing[0][7] = 1;
    struct gfx_trace_event event = {.w = 8, .h = 2, .y = 1};
    int rc = gfx_snapshot_take(3, &event) || !head;
    if (!rc) {
        for (int x = 0; x < 16; x++)
            if (head->data[x] != (x < 4 ? 1 << x : 0) ||
                head->data[16 + x] != (x == 15 ? 8 : 0)) rc = 1;
        gfx_snapshot_emit(3);
        if (head || released != 1 || allocated != 1) rc = 1;
    }
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
