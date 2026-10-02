/* T2h h2 preparation. main is first; h3 can reuse the live-frame callback. */
#include "os32api.h"
#include "os32_gui_shared.h"
#include "memmap.h"
#include "h2_stack_probe.h"
#include <string.h>
#ifndef H2_STACK_BYTES
#define H2_STACK_BYTES MEM_EXEC_STACK_SIZE
#endif
#define H2_WAIT_TICKS 100U
static int h2_argv_ok(struct h2_probe *p);
static int wait_live(struct h2_probe *p, volatile unsigned char *frame, unsigned int bytes);

int main(int argc, char **argv, KernelAPI *api)
{
    struct h2_probe p;
    unsigned int bytes, depth;
    unsigned long guard_va;
    int ok;
    h2_plan(H2_STACK_BYTES, &bytes, &depth, &guard_va);
    api->kprintf(H2_ATTR, "H2 entry stack=%u argc=%d\n", (unsigned int)H2_STACK_BYTES, argc);
    if (!h2_entry_ok(argc) || (strcmp(argv[1], "run") && strcmp(argv[1], "park") && strcmp(argv[1], "guard"))) {
        api->kprintf(H2_ATTR, "usage: h2_stack[512] run|park|guard h2-arg\n");
        return 2;
    }
    p.argc = argc;
    p.argv = argv;
    p.context = api;
    p.wait = strcmp(argv[1], "park") == 0 ? wait_live : 0;
    ok = h2_probe_run(&p, bytes, depth);
    api->kprintf(H2_ATTR, "H2 patterns=%s argv=%s pages=%u span=%u low=%x high=%x\n",
                 ok ? "OK" : "FAIL", h2_argv_ok(&p) ? "OK" : "FAIL", p.pages,
                 (unsigned int)(p.high - p.low), (unsigned int)p.low, (unsigned int)p.high);
    if (!ok) return 1;
    if (strcmp(argv[1], "guard") == 0) {
        volatile u8 *guard = (volatile u8 *)guard_va;
        api->kprintf(H2_ATTR, "H2 guard addr=%x expect PF error=6\n", (unsigned int)guard);
        *guard = H2_PATTERN;
        api->kprintf(H2_ATTR, "H2 guard SURVIVED FAIL\n");
        return 1;
    }
    api->kprintf(H2_ATTR, "H2 exit OK; PM: check this owner used=0 after exit\n");
    return 0;
}

#include "h2_stack_probe.inc"

static int wait_live(struct h2_probe *p, volatile unsigned char *frame, unsigned int bytes)
{
    KernelAPI *api = (KernelAPI *)p->context;
    GuiWinSpec spec;
    volatile u8 *req;
    volatile GuiSlotHeader *header;
    u32 base, i;
    i32 slot, win, rc;
    (void)frame;
    (void)bytes;
    slot = api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    if (slot < 0) return 0;
    base = api->shm_base + GUI_SHM_OFFSET + (u32)slot * GUI_SLOT_SIZE;
    req = (volatile u8 *)(base + GUI_SLOT_REQ_OFF);
    memset(&spec, 0, sizeof(spec));
    strcpy((char *)spec.title, "h2 stack live");
    spec.rect.x = 40; spec.rect.y = 40; spec.rect.w = 240; spec.rect.h = 80;
    spec.flags = GUI_WF_DEFAULT;
    for (i = 0; i < sizeof(spec); i++) req[i] = ((u8 *)&spec)[i];
    win = api->gui_call(GUI_OP_WIN_CREATE, 0);
    if (win < 0) return 0;
    api->kprintf(H2_ATTR, "H2 park live slot=%d win=%x low=%x\n", slot, win, (unsigned int)p->low);
    /* Consume creation/focus/paint events so the first wait can really park.
     * h3 supplies its own SHM-controlled callback at this same live frame. */
    api->gui_call(GUI_OP_POLL, 0);
    header = (volatile GuiSlotHeader *)(base + GUI_SLOT_HDR_OFF);
    header->ring_head = header->ring_tail;
    rc = api->gui_call(GUI_OP_WAIT, H2_WAIT_TICKS);
    api->kprintf(H2_ATTR, "H2 resume wait=%d; PM: require actual park/switch delta\n", rc);
    api->gui_call(GUI_OP_WIN_DESTROY, (u32)win);
    return rc >= 0;
}
