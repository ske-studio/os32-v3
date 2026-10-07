#include "os32api.h"
#include "os32_gui_shared.h"
#include "memmap.h"
#include "protocol.h"
static int h3_run(KernelAPI *api);
static void h3_fire(KernelAPI *api, u32 mode);

/* First function, including in the translation unit. No Run... arguments. */
int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc;
    (void)argv;
    return h3_run(api) ? 1 : 0;
}
static KernelAPI *h3_api;
#include "state.inc"

static int h3_run(KernelAPI *api)
{
    h3_api = api;
    i32 slot = api->gui_call(GUI_OP_INIT, GUI_PROTO_VERSION);
    volatile H3Block *b;
    volatile GuiSlotHeader *hdr;
    volatile u8 *req;
    GuiWinSpec spec = {0};
    u32 i, mode, start;
    const char *title = H3_TITLE;
    if (slot < 0) return 1;
    b = api->sys_shm_alloc(1);
    if (!b) return 2;
    for (i = 0; i < sizeof(*b) / sizeof(u32); i++)
        ((volatile u32 *)b)[i] = 0;
    hdr = (volatile GuiSlotHeader *)(api->shm_base + GUI_SHM_OFFSET +
                                     (u32)slot * GUI_SLOT_SIZE);
    req = (volatile u8 *)hdr + GUI_SLOT_REQ_OFF;
    for (i = 0; title[i] && i < sizeof(spec.title) - 1; i++)
        spec.title[i] = (u8)title[i];
    spec.rect.x = H3_FIXTURE == 1 ? 30 : 340;
    spec.rect.y = 60;
    spec.rect.w = 250;
    spec.rect.h = 130;
    spec.flags = GUI_WF_DEFAULT;
    for (i = 0; i < sizeof(spec); i++) req[i] = ((u8 *)&spec)[i];
    i32 win = api->gui_call(GUI_OP_WIN_CREATE, 0);
    if (win < 0) { api->sys_shm_free((void *)b); return 3; }
    b->window = (u32)win;
    b->gui_slot = (u32)slot;
    b->version = H3_VERSION;
    b->fixture = H3_FIXTURE;
    b->phase = H3_INIT;
    b->magic = H3_MAGIC;              /* publish last */
    /* Initialization yield is WAIT_POLL, not the OP_WAIT under test. */
    while (!h3_identity(b)) api->sys_yield();
    for (;;) {
        /* Discard GUI events; no key chooses or arms a mode. */
        api->gui_call(GUI_OP_POLL, 0);
        hdr->ring_head = hdr->ring_tail;
        b->phase = H3_WAIT;
        api->gui_call(GUI_OP_WAIT, 0); /* no pre-arm timer */
        b->resumes++;
        b->phase = H3_RESUMED;
        mode = h3_consume(b);
        if (b->phase == H3_ERROR) return 4;
        if (mode) break;
    }
    /* Only after consuming arm. Host has five seconds to observe resume;
     * WM pumps at KAPI boundaries. USER loop itself contains no KAPI. */
    start = api->get_tick();
    while ((u32)(api->get_tick() - start) < H3_GRACE_TICKS) { }
    b->phase = H3_FIRING;
    h3_fire(api, mode);
    b->phase = H3_ERROR;              /* fault unexpectedly survived */
    return 5;
}

static void h3_fire(KernelAPI *api, u32 mode)
{
    switch (mode) {
    case H3_PF:
        *(volatile u32 *)KERNEL_LOAD_ADDR = H3_MAGIC;
        break;
    case H3_GP:
        __asm__ volatile("hlt");
        break;
    case H3_DE: {
        u32 dividend = 1;
        __asm__ volatile("xorl %%edx, %%edx; divl %%ecx"
                         : "+a"(dividend) : "c"(0) : "edx", "cc");
        break;
    }
    case H3_UD:
        __asm__ volatile("ud2");
        break;
    case H3_USER_LOOP:
        for (;;) __asm__ volatile("" ::: "memory");
    case H3_KAPI_LOOP:
        for (;;) api->get_tick();
    }
}
