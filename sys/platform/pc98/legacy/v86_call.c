#include "v86_call.h"
#include "kernel_tick.h"
#include "kapi_call.h"
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
#include "snd2_fmq.h"
#include "platform_fm_probe.h"
#include "sys.h"
#ifdef TICK_PROBE
#include "tick_probe.h"
#include "tsc_clock.h"
#endif

#define V86_CALL_MAX_PATHS 2

extern volatile int ring3_in_syscall, ring3_wm_depth;

/* Allocated once at boot, retained by core outside the low guest area. */
static void *mailbox;
static struct {
    struct caller_access caller;
    u32 cr3;
    int pinned;
    char *path[V86_CALL_MAX_PATHS];
    V86Gcap *capture;
} call;

static struct soft_boot {
    struct v86_context ctx;
    SND2_FmQ events;
    u32 sid;
    int finished;
    u8 index[2];
} *soft;
static u32 next_sid;

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
    for (u32 i = 0; i < V86_CALL_MAX_PATHS; i++) {
        if (call.path[i]) kfree(call.path[i]);
        call.path[i] = 0;
    }
    if (call.capture) kfree(call.capture);
    call.capture = 0;
    if (soft) {
#ifdef TICK_PROBE
        tick_probe_soft_session(0, 0, 0);
#endif
        kfree(soft);
        soft = 0;
    }
    call.pinned = 0;
}

int v86_call_dispatch(u32 slot, void *target, struct kapi_arg_source *src, unsigned count,
                      u32 *result)
{
    struct kapi_values values;
    u32 *staged = values.value, output = 0;
    int paths = 0, capture = 0, handled = 1;
    switch (slot) {
    case KAPI_SLOT_V86_SELFTEST: break;
    case KAPI_SLOT_V86_DISKTEST:
    case KAPI_SLOT_V86_BOOT: paths = 1; break;
    case KAPI_SLOT_V86_BOOT2: paths = V86_CALL_MAX_PATHS; break;
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
    if (count != (unsigned)(capture ? 2 : paths) ||
        !arch_kapi_read_block(src, 0, count, &values)) {
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
    if (!shared_stack((uptr)&values) || !shared_stack(tss_get_esp0()))
        goto out;
    paging_load_cr3(paging_kernel_pd_phys());
    *result = capture ? (u32)v86_gdc_capture((int)staged[0], call.capture)
                      : arch_kapi_invoke(target, &values, count);
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

static int soft_candidate(void)
{
    if (!platform_fm_irq12_released()) return 0;
    SND2_DevInfo info;
    for (u32 i = 0; !kapi_snd2_dev_info(i, &info); ++i)
        if (info.kind == SND2_KIND_PCM_OUT &&
            (info.flags & (SND2_DEV_SOFT_CPU | SND2_DEV_READY)) ==
            (SND2_DEV_SOFT_CPU | SND2_DEV_READY)) return 1;
    return 0;
}

static int terminated_path(const char *path)
{
    for (unsigned i = 0; i < sizeof(((V86_Begin *)0)->path); ++i)
        if (!path[i]) return 1;
    return 0;
}

int v86s_begin(const V86_Begin *input, u32 *sid)
{
    if (!input || !sid || input->size != sizeof *input || input->flags != V86S_FM_SOFT)
        return OS32_ERR_INVAL;
    if (!soft_candidate()) return OS32_ERR_NOSYS;
    if (call.pinned || !v86_call_admit()) return OS32_ERR_BUSY;
    V86_Begin b = *input;
    if (!b.path[0] || !terminated_path(b.path) || !terminated_path(b.second))
        return OS32_ERR_INVAL;
    if (!caller_access_get_user(&call.caller) || !shared_stack((uptr)&b) ||
        !shared_stack(tss_get_esp0())) return OS32_ERR_BUSY;
    soft = kzalloc(sizeof *soft);
    if (!soft) return OS32_ERR_NOSPC;
    call.cr3 = paging_current_cr3();
    call.pinned = 1;
#ifdef TICK_PROBE
    u32 mult = 0, shift = 0, generation;
    (void)pc98_tsc_rate(&mult, &shift, &generation);
    tick_probe_soft_session(1, mult, shift);
#endif
    paging_load_cr3(paging_kernel_pd_phys());
    int rc = v86_boot_prepare(b.path, b.second[0] ? b.second : 0, &soft->ctx);
    if (!rc && v86_mem_enable_slices()) {
        v86_session_end();
        rc = OS32_ERR_NOSPC;
    }
    if (rc) {
        v86_call_cleanup();
        return rc;
    }
    for (u32 port = 0x188; port <= 0x18f; ++port) tss_iomap_deny(port);
    v86_session.ticks = 0;
    v86_session.soft_jiffy = tick_count;
    v86_session.soft_input = 1;
    v86_mem_suspend();
    paging_load_cr3(call.cr3);
    soft->sid = ++next_sid;
    *sid = soft->sid;
    return 0;
}

static int soft_owner(u32 sid)
{
    struct caller_access live;
    return soft && sid == soft->sid && caller_access_get_user(&live) &&
           live.as == call.caller.as && live.generation == call.caller.generation;
}

int v86s_run(u32 sid, const V86_RunIn *in, V86_Exit *out)
{
    if (!soft_owner(sid) || !in || !out || in->size != sizeof *in ||
        in->reply_valid || soft->finished) return OS32_ERR_INVAL;
    int reason = V86_EXIT_SLICE;
    if (v86_session.soft_stop) reason = V86_EXIT_HOTKEY;
    else if (tick_count - v86_session.soft_jiffy >= V86_TICK_LIMIT) reason = V86_EXIT_TIMEOUT;
    else if (in->slice_ms) {
#ifdef TICK_PROBE
        tick_probe_soft_begin(TP_SOFT_ENTER);
#endif
        paging_load_cr3(paging_kernel_pd_phys());
        v86_mem_resume();
        reason = v86_run_slice(&soft->ctx);
        v86_mem_suspend();
        paging_load_cr3(call.cr3);
#ifdef TICK_PROBE
        tick_probe_soft_end(TP_SOFT_LEAVE);
#endif
    }
    u32 hi;
    *out = (V86_Exit){.size = sizeof *out, .code = reason};
    out->fifo_n = soft->events.count;
    if (sys_time_now(&out->guest_us, &hi)) return OS32_ERR_IO;
    if (reason == V86_EXIT_SLICE) out->reason = V86S_SLICE;
    else if (reason == V86_EXIT_FIFO) out->reason = V86S_FIFO;
    else {
        soft->finished = 1;
        out->reason = reason == V86_EXIT_HLT || reason == V86_EXIT_HOTKEY ||
                      reason == V86_EXIT_TRAP_PORT ? V86S_END : V86S_FAULT;
    }
    return 0;
}

int v86s_fm_drain(u32 sid, SND2_FmEv *out, u32 max, u32 *last_seq)
{
    if (!soft_owner(sid) || max > SND2_FMQ_LEN || (max && !out) || !last_seq)
        return OS32_ERR_INVAL;
    u32 n = snd2_fmq_drain(&soft->events, out, max);
    *last_seq = soft->events.sequence - soft->events.count;
    return (int)n;
}

int v86s_end(u32 sid)
{
    if (!soft_owner(sid)) return OS32_ERR_INVAL;
    paging_load_cr3(paging_kernel_pd_phys());
    v86_session_end();
    v86_call_cleanup();
    return 0;
}

/* Return -1 to retry a data byte after draining the full queue. */
int v86_soft_out(u16 port, u8 value)
{
    if (!soft || port < 0x188 || port > 0x18f) return 0;
    if (port & 1) return 1;
    unsigned bank = (port >> 2) & 1;
    if (!(port & 2)) {
        soft->index[bank] = value;
        return 1;
    }
    u32 now, hi;
    if (sys_time_now(&now, &hi)) return -2;
    return snd2_fmq_push(&soft->events, 1, bank, soft->index[bank], value, now) ? 1 : -1;
}
