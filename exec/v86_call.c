#include "v86_call.h"
#include "appslot.h"
#include "redir_access.h"
#include "v86_mem.h"
#include "v86.h"
#include "v86_gcap.h"
#include "os32_kapi_slots.h"
#include "con_sink.h"
#include "kmalloc.h"
#include "vfs.h"
#include "tss.h"
#include "kprintf.h"

extern volatile int ring3_in_syscall, ring3_wm_depth;
extern u32 kapi_invoke(void *target, const void *args, u32 bytes);

/* Allocated once at boot, retained by core outside the low guest area. */
static void *mailbox;
static struct {
    struct caller_access caller;
    u32 cr3;
    int pinned;
    char *path[2];
    V86Gcap *capture;
} call;

static int shared_stack(uptr sp)
{
    return sp >= MEM_CONV_END && sp < MEM_APP_BAND_BASE;
}

int v86_call_init(void)
{
    if (!mailbox) mailbox = kmalloc(MEM_AUTOPLAY_MAILBOX_SIZE);
    return mailbox != 0;
}

void *v86_call_mailbox(void) { return mailbox; }

int v86_call_pinned(void) { return call.pinned; }

int v86_call_admit(void)
{
    struct caller_access c;
    AppSlot *a = appslot_get(appslot_cur());
    if (con_sink_is_enabled() || ring3_wm_depth || !a || a->gui ||
        a->state != APP_STATE_RUNNING) return 0;
    if (call.pinned)
        return a->as == call.caller.as && appslot_cur() == call.caller.app_id;
    if (v86_session.open || v86_session.closing || v86_session.release_pending)
        return 0;
    if (!ring3_in_syscall)
        return appslot_cur() == APP_ID_SHELL && !paging_live_addrspaces();
    if (!caller_access_get_user(&c) || a->as != c.as ||
        a->parent != APP_ID_SHELL || paging_live_addrspaces() != 1) return 0;
    for (int id = APP_ID_MIN; id <= APP_ID_MAX; id++)
        if (id != c.app_id && appslot_get(id)) return 0;
    return 1;
}

/* Called after session restoration, including the normal-stack fault landing.
 * Never frees on an exception stack, never dereferences a saved caller VA. */
void v86_call_cleanup(void)
{
    if (!call.pinned) return;
    paging_load_cr3(call.cr3);
    for (u32 i = 0; i < 2; i++) {
        if (call.path[i]) kfree(call.path[i]);
        call.path[i] = 0;
    }
    if (call.capture) kfree(call.capture);
    call.capture = 0;
    call.pinned = 0;
}

int v86_call_dispatch(u32 slot, void *target, const void *args, u32 bytes,
                      u32 *result)
{
    u32 staged[2] = {0}, output = 0;
    int paths = 0, capture = 0, handled = 1;
    switch (slot) {
    case KAPI_SLOT_V86_SELFTEST: break;
    case KAPI_SLOT_V86_DISKTEST:
    case KAPI_SLOT_V86_BOOT: paths = 1; break;
    case KAPI_SLOT_V86_BOOT2: paths = 2; break;
    case KAPI_SLOT_V86_GDC_CAPTURE: capture = 1; break;
    default: return 0;
    }
    *result = OS32_ERR_BUSY;
    if (call.pinned) return 1;
    if (con_sink_is_enabled()) {
        v86_gui_refuse(); /* Preserve the existing CUI-only abort convention. */
        *result = capture ? (u32)OS32_ERR_INVAL : (u32)-1;
        return 1;
    }
    if (!v86_call_admit()) {
        kprintf(ATTR_RED, "v86: busy; run directly from the CUI shell\n");
        return 1;
    }
    if (!mailbox) { *result = OS32_ERR_NOSPC; return 1; }
    if (!caller_access_get_user(&call.caller)) return 1;
    call.cr3 = paging_current_cr3();
    call.pinned = 1;
    *result = OS32_ERR_INVAL;
    if (bytes != (u32)(capture ? 2 : paths) * sizeof(u32) ||
        !copy_caller_bytes(&call.caller, args, staged, bytes)) {
        handled = -1;
        goto out;
    }
    for (int i = 0; i < paths; i++) {
        /* NULL paths keep the existing disk/boot API error convention. */
        if (!staged[i]) continue;
        call.path[i] = kmalloc(VFS_MAX_PATH);
        if (!call.path[i]) { *result = OS32_ERR_NOSPC; goto out; }
        if (!copy_caller_cstr(&call.caller, (const char *)(uptr)staged[i],
                              call.path[i], VFS_MAX_PATH)) {
            handled = -1;
            goto out;
        }
        staged[i] = (u32)(uptr)call.path[i];
    }
    if (capture) {
        output = staged[1];
        if (!output) goto out; /* Existing capture(NULL) returns INVAL. */
        if (!check_caller_write_range(&call.caller,
                (void *)(uptr)output, sizeof(V86Gcap))) {
            handled = -1;
            goto out;
        }
        call.capture = kzalloc(sizeof(V86Gcap));
        if (!call.capture) { *result = OS32_ERR_NOSPC; goto out; }
        staged[1] = (u32)(uptr)call.capture;
    }
    /* int80 runs on the shared core stack. Its saved KAPI frame and ESP0
     * remain there while caller-private mappings are absent from master. */
    if (!shared_stack((uptr)&staged) || !shared_stack(tss_get_esp0()))
        goto out;
    paging_load_cr3(paging_kernel_pd_phys());
    *result = capture ? (u32)v86_gdc_capture((int)staged[0], call.capture)
                      : kapi_invoke(target, staged, bytes);
    paging_load_cr3(call.cr3);
    struct caller_access live;
    if (!caller_access_get_user(&live) || live.as != call.caller.as ||
        live.generation != call.caller.generation) {
        *result = OS32_ERR_STALE;
        goto out;
    }
    if (capture && (int)*result >= 0 &&
        !copy_to_caller(&call.caller, (void *)(uptr)output,
                        call.capture, sizeof(V86Gcap))) {
        *result = OS32_ERR_INVAL;
        handled = -1;
    }
out:
    v86_call_cleanup();
    return handled;
}
