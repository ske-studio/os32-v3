/* T2g / GK-3: raw GUI back, kernel ARENA tail/whole + nano tail fixture. */
#include "os32api.h"
#include "os32_gui_shared.h"
#include "memmap.h"
#include "nano_adapter.h"
#include <stdlib.h>
#include <string.h>

#define ATTR 0x07
#define BLOCK_BYTES 4000U
#define BLOCK_LIMIT 128U
#define CACHE_T 8U
#define NANO_COUNT 16U
#define NANO_KEEP 4U
/* MemStat.extents ordering and exec_heap's two-u32 BlkHdr wire format. */
enum { INITIAL = 1, ARENA = 3, LARGE = 4, BLOCK_HEADER_BYTES = 2 * sizeof(u32) };
static KernelAPI *back_api;
static void *blocks[BLOCK_LIMIT], *nano[NANO_COUNT];
static u32 count, keep, obstacle, baseline_crc, slow_ticks;
static u32 serves, pages, hooks, consumed, nano_pages;
static int armed, released, bad, regrow_rc = -1, no_answer, stop_focus;
static i32 own_window;
static MemStat initial, prepared, trimmed;
static int snapshot(MemStat *out);
static int prepare(void);
static uint32_t trim_hook(void);
static u32 data_crc(void);
static int serve(u32 epoch);
static int regrow(void);
static int run_gui(void);

/* OS32X: main must be the first function in the translation unit. */
int main(int argc, char **argv, KernelAPI *api)
{
    back_api = api;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--no-answer")) no_answer = 1;
        else if (!strcmp(argv[i], "--stop-focus")) stop_focus = 1;
        else if (!strcmp(argv[i], "--slow-hook") && i + 1 < argc) {
            char *end;
            slow_ticks = strtoul(argv[++i], &end, 10);
            if (!argv[i][0] || *end || slow_ticks > 0x7fffffffUL) return 2;
        } else {
            api->kprintf(ATTR, "usage: trim_back [--slow-hook N] [--stop-focus] [--no-answer]\n");
            return 2;
        }
    }
    int rc = run_gui();
    api->kprintf(ATTR, "trim_back exit trim_serve=%u pages=%u hook=%u CRC=%s regrow rc=%d consumed=%u nano_pages=%u\n",
                 (unsigned)serves, (unsigned)pages, (unsigned)hooks,
                 bad ? "BAD" : "OK", regrow_rc, (unsigned)consumed, (unsigned)nano_pages);
    return rc || bad;
}

static int snapshot(MemStat *out)
{
    return back_api->mem_stat(-1, out, sizeof(*out)) == sizeof(*out) &&
           !(out->flags & (MEMSTAT_POISONED | MEMSTAT_HEAP_INVALID));
}

static u32 data_crc(void)
{
    u32 crc = ~0UL;
    for (u32 i = 0; i < keep + NANO_KEEP; i++) {
        const u8 *p = i < keep ? blocks[i] : nano[i - keep];
        for (u32 j = 0; j < BLOCK_BYTES; j++) {
            crc ^= p[j];
            for (u32 b = 0; b < 8; b++)
                crc = (crc >> 1) ^ (0xedb88320UL & (0UL - (crc & 1)));
        }
    }
    return ~crc;
}

static int prepare(void)
{
    MemStat s;
    u32 arena1_count = 0;
    if (!snapshot(&initial)) return 0;
    while (count < BLOCK_LIMIT) {
        void *p = back_api->mem_alloc(BLOCK_BYTES);
        if (!p) return 0;
        blocks[count++] = p;
        if (!snapshot(&s)) return 0;
        if (!obstacle && s.extents[ARENA] == 1) {
            obstacle = s.exec_heap_cur_end;
            if ((u32)back_api->mem_map(OS32_PAGE_SIZE, (void *)obstacle, OS32_MEM_MAP_EXACT) != obstacle)
                return 0;
        }
        if (s.extents[ARENA] == 2) break;
    }
    if (!obstacle || s.extents[ARENA] != 2 || s.extents[LARGE]) return 0;
    /* Classification is by returned address, never by allocation ordinal. */
    for (u32 i = 0; i < count; i++)
        if ((u32)blocks[i] < obstacle) arena1_count++;
    if (arena1_count <= CACHE_T || arena1_count == count) return 0;
    /* Put the address classes in order; last eight ARENA1 blocks are cache-T. */
    for (u32 i = 0; i < count; i++)
        for (u32 j = i + 1; j < count; j++)
            if ((u32)blocks[j] < (u32)blocks[i]) {
                void *p = blocks[i]; blocks[i] = blocks[j]; blocks[j] = p;
            }
    keep = arena1_count - CACHE_T;
    if ((u32)blocks[keep] < initial.exec_heap_cur_end) return 0;
    for (u32 i = 0; i < NANO_COUNT; i++) {
        nano[i] = malloc(BLOCK_BYTES);
        if (!nano[i]) return 0;
    }
    for (u32 i = 0; i < keep + NANO_KEEP; i++) {
        u8 *p = i < keep ? blocks[i] : nano[i - keep];
        for (u32 j = 0; j < BLOCK_BYTES; j++) p[j] = (u8)(i ^ j ^ 0xa5);
    }
    baseline_crc = data_crc();
    if (!snapshot(&prepared) || prepared.extents[ARENA] != 2 || prepared.extents[LARGE])
        return 0;
    back_api->kprintf(ATTR, "trim_back PREP OK id=%d ARENA=2 LARGE=0 cur=%x cache-T=%u cache-E=%u cache-N=%u CRC=%x\n",
                     (int)prepared.app_id, (unsigned)prepared.exec_heap_cur_end,
                     CACHE_T, (unsigned)(count - arena1_count), NANO_COUNT - NANO_KEEP,
                     (unsigned)baseline_crc);
    return 1;
}

static uint32_t trim_hook(void)
{
    if (!armed || released) return 0;
    hooks++;
    if (stop_focus) {
        i32 rc = back_api->gui_call(GUI_OP_WIN_SET_FOCUS, (u32)own_window);
        if (rc != 0) {
            back_api->kprintf(ATTR, "trim_back focus FAIL rc=%d\n", (int)rc);
            bad = 1;
            return 0;
        }
        back_api->kprintf(ATTR, "trim_back focus ok\n");
    }
    back_api->kprintf(ATTR, "trim_back hook begin slow=%u\n", (unsigned)slow_ticks);
    u32 start = back_api->get_tick();
    while ((u32)(back_api->get_tick() - start) < slow_ticks) { }
    for (u32 i = keep; i < count; i++) back_api->mem_free(blocks[i]);
    for (u32 i = NANO_KEEP; i < NANO_COUNT; i++) free(nano[i]);
    released = 1;
    if (data_crc() != baseline_crc) bad = 1;
    back_api->kprintf(ATTR, "trim_back DATA %s CRC=%x\n", bad ? "BAD" : "OK", (unsigned)data_crc());
    /* serve trimmed before calling us; reclaim the newly freed nano tail now. */
    uint32_t returned = os32_nano_trim();
    nano_pages += returned;
    back_api->kprintf(ATTR, "trim_back nano tail pages=%u\n", (unsigned)returned);
    if (!returned) bad = 1;
    return returned;
}

static int serve(u32 epoch)
{
    MemStat before, after;
    consumed++;
    if (no_answer) {
        back_api->kprintf(ATTR, "trim_back TRIM consumed epoch=%u no-answer\n", (unsigned)epoch);
        return 1;
    }
    if (!snapshot(&before)) return 0;
    int first_release = armed && !released;
    uint32_t pre_hook_pages = os32_gui_trim_serve(epoch, trim_hook);
    nano_pages += pre_hook_pages;
    serves++;
    if (!snapshot(&after) || after.phys_free_pages < before.phys_free_pages) return 0;
    u32 returned = after.phys_free_pages - before.phys_free_pages;
    pages += returned;
    back_api->kprintf(ATTR, "trim_back DONE epoch=%u pages=%u armed=%d pending=%u\n",
                     (unsigned)epoch, (unsigned)returned, armed,
                     (unsigned)((after.flags & MEMSTAT_TRIM_PENDING) != 0));
    if (after.flags & MEMSTAT_TRIM_PENDING) return 0;
    if (first_release) {
        trimmed = after;
        int ok = before.extents[ARENA] == 2 && after.extents[ARENA] == 1 &&
                 after.exec_heap_cur_end < before.exec_heap_cur_end &&
                 after.extents[INITIAL] == initial.extents[INITIAL] &&
                 after.exec_heap_base == initial.exec_heap_base &&
                 after.exec_heap_size == initial.exec_heap_size &&
                 !after.extents[LARGE] && !bad;
        back_api->kprintf(ATTR, "trim_back G-4 %s cur=%x->%x ARENA=%u->%u INITIAL=%u->%u DATA=%s; r=regrow after observation\n",
                         ok ? "OK" : "FAIL", (unsigned)before.exec_heap_cur_end,
                         (unsigned)after.exec_heap_cur_end, (unsigned)before.extents[ARENA],
                         (unsigned)after.extents[ARENA], (unsigned)initial.extents[INITIAL],
                         (unsigned)after.extents[INITIAL], bad ? "BAD" : "OK");
        if (!ok) return 0;
    }
    return 1;
}

static int regrow(void)
{
    MemStat after = {0};
    if (!released || regrow_rc != -1) return 1;
    regrow_rc = 1;
    /* PM decision: G-4 is printed and inspected before this separate key. */
    int rc = back_api->mem_unmap((void *)obstacle, OS32_PAGE_SIZE);
    back_api->kprintf(ATTR, "trim_back obstacle unmapped rc=%d pages=1 (excluded from trim pages)\n", rc);
    if (rc) return 0;
    void *p = back_api->mem_alloc(BLOCK_BYTES);
    int ok = snapshot(&after) && (u32)p == trimmed.exec_heap_cur_end + BLOCK_HEADER_BYTES &&
             after.exec_heap_cur_end == trimmed.exec_heap_cur_end + MEM_EXEC_HEAP_MIN &&
             after.extents[ARENA] == 1 && !after.extents[LARGE] && data_crc() == baseline_crc;
    regrow_rc = ok ? 0 : 1;
    back_api->kprintf(ATTR, "trim_back regrow %s rc=%d ptr=%x cur=%x->%x ARENA=%u EXACT\n",
                     ok ? "ok" : "FAIL", regrow_rc, (unsigned)p,
                     (unsigned)trimmed.exec_heap_cur_end, (unsigned)after.exec_heap_cur_end,
                     (unsigned)after.extents[ARENA]);
    return ok;
}

static int run_gui(void)
{
    i32 slot = back_api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot < 0) goto prep_fail;
    u32 base = back_api->shm_base + GUI_SHM_OFFSET + (u32)slot * GUI_SLOT_SIZE;
    volatile GuiSlotHeader *hdr = (volatile GuiSlotHeader *)base;
    volatile GuiEvent *ring = (volatile GuiEvent *)(base + GUI_SLOT_RING_OFF);
    volatile u8 *req = (volatile u8 *)(base + GUI_SLOT_REQ_OFF);
    GuiWinSpec spec = {0};
    strcpy((char *)spec.title, "trim_back: a arm / r regrow / q quit");
    spec.rect.x = 30; spec.rect.y = 50; spec.rect.w = 350; spec.rect.h = 100;
    spec.flags = GUI_WF_DEFAULT;
    for (u32 i = 0; i < sizeof(spec); i++) req[i] = ((u8 *)&spec)[i];
    i32 win = back_api->gui_call(GUI_OP_WIN_CREATE, 0);
    if (win < 0) goto prep_fail;
    own_window = win;
    if (!prepare()) goto prep_fail;
    back_api->kprintf(ATTR, "trim_back keys: a=arm r=regrow after G-4 q=quit\n");
    for (;;) {
        if (back_api->gui_call(GUI_OP_POLL, 0) < 0) return 1;
        while (hdr->ring_head != hdr->ring_tail) {
            GuiEvent event = ring[hdr->ring_head % GUI_RING_CAPACITY];
            hdr->ring_head++;
            if (event.kind == GUI_EV_TRIM) {
                u32 epoch = 0;
                for (u32 i = 0; i < 4; i++) epoch |= (u32)event.payload.raw[i] << (8 * i);
                if (!serve(epoch)) { bad = 1; return 1; }
            } else if (event.kind == GUI_EV_CLOSE || event.kind == GUI_EV_QUIT ||
                       (event.kind == GUI_EV_KEY && event.sub && event.payload.key.ch == 'q')) {
                back_api->gui_call(GUI_OP_WIN_DESTROY, (u32)win);
                return 0;
            } else if (event.kind == GUI_EV_KEY && event.sub) {
                if (event.payload.key.ch == 'a') {
                    armed = 1;
                    back_api->kprintf(ATTR, "trim_back arm accepted\n");
                } else if (event.payload.key.ch == 'r' && !regrow()) { bad = 1; return 1; }
            }
        }
        if (back_api->gui_call(GUI_OP_WAIT, 0) < 0) return 1;
    }
prep_fail:
    bad = 1;
    back_api->kprintf(ATTR, "trim_back PREP FAIL\n");
    return 1;
}
