/* T2g / GK-3: preparation is separate from the single target allocation. */
#include "os32api.h"
#include "os32_gui_shared.h"
#include "memmap.h"
#include "nano_adapter.h"
#include <stdlib.h>
#include <string.h>

#define ATTR 0x07
#define BLOCK_BYTES 4000U
#define MAP_BIG (1024U * 1024U)
#define MAP_MEDIUM (64U * 1024U)
#define CHILD_COMMAND "/usr/bin/trim_front.bin --child"
static KernelAPI *front_api;
static int snapshot(MemStat *out);
static int prepare(void);
static int target(int child);
static int prep_observe(void);
static int run_gui(void);

/* OS32X: main must be first; --cui never initializes a GUI slot. */
int main(int argc, char **argv, KernelAPI *api)
{
    front_api = api;
    if (argc == 2 && !strcmp(argv[1], "--cui")) {
        int kind, code;
        int rc = api->exec_run(CHILD_COMMAND);
        if (rc || api->exec_last_result(&kind, &code) || kind != EXEC_KIND_EXITED || code) {
            api->kprintf(ATTR, "trim_front CUI child FAIL exec rc=%d\n", rc);
            return 1;
        }
        api->kprintf(ATTR, "trim_front CUI child OK; parent terminal OP_WAIT delivers back marks\n");
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--child")) {
        if (!prepare()) return 1;
        return target(1);
    }
    if (argc != 1) {
        api->kprintf(ATTR, "usage: trim_front [--cui]\n");
        return 2;
    }
    return run_gui();
}

static int snapshot(MemStat *out)
{
    return front_api->mem_stat(-1, out, sizeof(*out)) == sizeof(*out) &&
           !(out->flags & (MEMSTAT_POISONED | MEMSTAT_HEAP_INVALID));
}

static int prepare(void)
{
    const u32 sizes[] = {MAP_BIG, MAP_MEDIUM, OS32_PAGE_SIZE};
    MemStat s;
    u32 mapped = 0, allocated = 0;
    front_api->kprintf(ATTR, "trim_front prep begin\n");
    for (u32 i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++)
        while (front_api->mem_map(sizes[i], 0, 0)) mapped += sizes[i] / OS32_PAGE_SIZE;
    if (!snapshot(&s) || s.phys_free_pages) goto fail;
    /* Retain every allocation until exit. The failed GUI malloc may yield to
     * unarmed backs, so keep draining until their natural tails are exhausted. */
    while (malloc(BLOCK_BYTES)) allocated++;
    if (!snapshot(&s) || s.phys_free_pages) goto fail;
    front_api->kprintf(ATTR, "trim_front PREP OK id=%d free_pages=0 raw_pages=%u malloc_blocks=%u\n",
                      (int)s.app_id, (unsigned)mapped, (unsigned)allocated);
    return 1;
fail:
    front_api->kprintf(ATTR, "trim_front PREP FAIL (requires mem_stat self and free_pages=0)\n");
    return 0;
}

/* Observe only after an OP_WAIT; a nonzero pending mask means DONE is still
 * outstanding. Never absorb newly returned pages here: p is an explicit action. */
static int prep_observe(void)
{
    MemStat s;
    if (!snapshot(&s)) return 0;
    front_api->kprintf(ATTR, "trim_front after WAIT free_pages=%u pending=%x\n",
                      (unsigned)s.phys_free_pages, (unsigned)s.trim_pending_mask);
    if (!s.phys_free_pages && !s.trim_pending_mask)
        front_api->kprintf(ATTR, "trim_front prep done; DONE settled; p=prepare t=target once q=quit\n");
    else
        front_api->kprintf(ATTR, "trim_front prep unsettled; observe back DONE then p=prepare\n");
    return 1;
}

static int target(int child)
{
    struct os32_gui_retry_stats before = os32_gui_retry_stats();
    void *p = malloc(BLOCK_BYTES);
    struct os32_gui_retry_stats after = os32_gui_retry_stats();
    unsigned retries = after.retry_count - before.retry_count;
    front_api->kprintf(ATTR, "trim_front %s retry=%u mode=%s\n", p ? "retry ok" : "ENOMEM",
                      retries, child ? "CUI" : "GUI");
    if (child) {
        MemStat s;
        return p || retries || !snapshot(&s) || (s.flags & MEMSTAT_TRIM_PENDING);
    }
    /* Both success and ENOMEM are observations; the contract is one retry. */
    return retries != 1;
}

static int run_gui(void)
{
    i32 slot = front_api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot < 0) goto fail;
    os32_gui_retry_enable();
    u32 base = front_api->shm_base + GUI_SHM_OFFSET + (u32)slot * GUI_SLOT_SIZE;
    volatile GuiSlotHeader *hdr = (volatile GuiSlotHeader *)base;
    volatile GuiEvent *ring = (volatile GuiEvent *)(base + GUI_SLOT_RING_OFF);
    volatile u8 *req = (volatile u8 *)(base + GUI_SLOT_REQ_OFF);
    GuiWinSpec spec = {0};
    strcpy((char *)spec.title, "trim_front: p prep / t target / q quit");
    spec.rect.x = 220; spec.rect.y = 170; spec.rect.w = 350; spec.rect.h = 100;
    spec.flags = GUI_WF_DEFAULT;
    for (u32 i = 0; i < sizeof(spec); i++) req[i] = ((u8 *)&spec)[i];
    i32 win = front_api->gui_call(GUI_OP_WIN_CREATE, 0);
    if (win < 0) goto fail;
    if (!prepare()) return 1;
    int tested = 0, rc = 0, observe_prep = 1;
    for (;;) {
        if (front_api->gui_call(GUI_OP_POLL, 0) < 0) return 1;
        int do_target = 0, do_prepare = 0;
        while (hdr->ring_head != hdr->ring_tail) {
            GuiEvent event = ring[hdr->ring_head % GUI_RING_CAPACITY];
            hdr->ring_head++;
            if (event.kind == GUI_EV_CLOSE || event.kind == GUI_EV_QUIT ||
                (event.kind == GUI_EV_KEY && event.sub && event.payload.key.ch == 'q')) {
                front_api->gui_call(GUI_OP_WIN_DESTROY, (u32)win);
                return rc;
            }
            if (event.kind == GUI_EV_KEY && event.sub && event.payload.key.ch == 't' && !tested) {
                do_target = 1;
            }
            if (event.kind == GUI_EV_KEY && event.sub && event.payload.key.ch == 'p' && !tested)
                do_prepare = 1;
        }
        /* Consume the entire ring, including key breaks, before allocating.
         * Later input may still preempt the retry: that is the WM contract. */
        if (do_prepare) {
            if (!prepare()) return 1;
            observe_prep = 1;
        } else if (do_target) {
            tested = 1;
            rc = target(0);
            if (rc) return rc;
        }
        if (front_api->gui_call(GUI_OP_WAIT, observe_prep ? 1 : 0) < 0) return 1;
        if (observe_prep) {
            if (!prep_observe()) return 1;
            observe_prep = 0;
        }
    }
fail:
    front_api->kprintf(ATTR, "trim_front PREP FAIL (GUI setup)\n");
    return 1;
}
