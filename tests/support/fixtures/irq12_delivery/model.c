#include "sys/platform/pc98/legacy/v86_pic.h"
#include "idt.h"
#include "sys/platform/pc98/legacy/v86_mem.h"
#include "sys/platform/pc98/legacy/v86.h"
#include "ksetjmp.h"
#include "appslot.h"
#include "redir_access.h"
#include "av_contract.h"
#include "v86_call.h"
#include "module.h"
#include "sys/dev/sound/fm/fm_module.c"

extern void fire_2(void), fire_12(void), fire_13(void);
volatile u32 kctx_irq_depth, tick_count;
unsigned vm, reflection, mouse, order, eoi, slave_mask = 255;
static unsigned slave_writes, master_writes;
static int fault_exit;
static unsigned requests;
struct v86_session_state v86_session;
u32 tss_get_esp0(void) { return 0x200000; }
void tss_set_esp0(u32 esp0) {}
int exec_setjmp(k_jmpbuf buf) { return 0; }
static void require(int ok, const char *message);
struct kdebug kdebug;
static int write_tone;
__attribute__((section(".guest"), aligned(16)))
static const u8 byte_out[16] = {0xee};
int v86_bios_is_hle(u32 vector) { return 0; }
void v86_bios_dispatch(u32 *frame, u32 vector) {}
u32 v86_io_in(u16 port, int size) { return 255; }
void v86_io_out(u16 port, int size, u32 value) { require(0, "FM port fell through"); }
static void guest_write(u16 port, u8 value)
{
    u32 gp[V86F_GS + 1] = {0};
    gp[V86F_CS] = (u32)byte_out >> 4;
    gp[V86F_EAX] = value;
    gp[V86F_EDX] = port;
    require(!v86_gp_handler(gp) && gp[V86F_EIP] == 1, "retire byte out");
}
extern u32 entered[17];
extern void check_enter(const struct v86_context *ctx);
static int slicing;
int v86_tick_and_check_timeout(void);
void __real_v86_reflect_irq(u32 *frame, u32 irq);
static unsigned suspended_ticks, expected_ticks;
void __real_v86_pic_request(u32 irq);
int __real_v86_pic_candidate(u32 *vector);
static u16 guest_ivt[512] __attribute__((section(".guestdata"), aligned(16)));
static u16 guest_stack[32] __attribute__((section(".guestdata"), aligned(16)));
static int test_timer, check_resume_timer;
void __wrap_v86_pic_request(u32 irq)
{
    if (!irq && !v86_is_active()) ++suspended_ticks;
    __real_v86_pic_request(irq);
}
int __wrap_v86_pic_candidate(u32 *vector)
{
    int irq = __real_v86_pic_candidate(vector);
    if (irq >= 0) *vector += (u32)guest_ivt / 4;
    return irq;
}
void v86_enter(const struct v86_context *ctx)
{
    require(suspended_ticks == expected_ticks, "foreground jiffy before VM");
    if (check_resume_timer) {
        require(ctx->eip == 0x123 && ctx->cs == 0x2000 && ctx->esp == 58 &&
                ctx->eflags == EFLAGS_VM && guest_stack[29] == 0x456 &&
                guest_stack[30] == 0x2100 && guest_stack[31] == EFLAGS_IF,
                "foreground timer frame");
        check_resume_timer = 0;
    }
    if (write_tone) {
        write_tone = 0;
        guest_write(0x188, 0x28);
        guest_write(0x18a, 0x10);
        guest_write(0x18a, 0);
    }
    if (slicing) {
        u32 frame[V86I_GS + 1];
        for (unsigned i = 0; i <= V86I_GS; ++i) frame[i] = 0x100 + i;
        frame[V86I_EFLAGS] = EFLAGS_VM | EFLAGS_IF;
        if (test_timer) {
            frame[V86I_SS] = (u32)guest_stack >> 4;
            frame[V86I_ESP] = sizeof guest_stack;
        }
        __real_v86_reflect_irq(frame, 0);
        require(v86_tick_and_check_timeout(), "timer return");
    }
    if (fault_exit) v86_runtime_end();
}
void kprintf(u8 attr, const char *fmt, ...) {}
void mouse_irq_handler(void) { ++mouse; }
void __wrap_v86_reflect_irq(u32 *frame, u32 irq) { ++requests; if (vm) reflection += irq; }
unsigned inp(unsigned port) { return port == 10 ? slave_mask : 255; }
void outp(unsigned port, unsigned value)
{
    if (port == 10) {
        ++slave_writes;
        slave_mask = value;
    }
    if (port == 2) ++master_writes;
    if ((port == 8 || port == 0) && value == 32)
        eoi = eoi * 10 + (port == 8 ? 2 : 1);
}
static void require(int ok, const char *message)
{
    if (ok) return;
    unsigned length = 0;
    while (message[length]) ++length;
    __asm__ volatile("int $0x80" :: "a"(4), "b"(2), "c"(message), "d"(length) : "memory");
    __asm__ volatile("int $0x80" :: "a"(1), "b"(1));
    __builtin_unreachable();
}
static u32 cr3 = 0x3000, now_us;
static int heap_busy, opens, suspends, resumes;
static AppSlot caller_slot;
static struct addrspace caller_as;
volatile int ring3_in_syscall = 1, ring3_wm_depth;
int con_sink_is_enabled(void) { return 0; }
int appslot_cur(void) { return APP_ID_MIN; }
AppSlot *appslot_get(int id) { return id == APP_ID_MIN ? &caller_slot : 0; }
u32 paging_live_addrspaces(void) { return 1; }
u32 paging_current_cr3(void) { return cr3; }
u32 paging_kernel_pd_phys(void) { return 0x4000; }
void paging_load_cr3(u32 value) { cr3 = value; }
int caller_access_get_user(struct caller_access *c)
{
    *c = (struct caller_access){.as = &caller_as, .app_id = APP_ID_MIN, .generation = 1};
    return 1;
}
void *kzalloc(u32 bytes)
{
    static u32 memory[2048];
    require(!heap_busy && bytes <= sizeof memory, "soft allocation");
    heap_busy = 1;
    for (unsigned i = 0; i < sizeof memory / sizeof *memory; ++i) memory[i] = 0;
    return memory;
}
void kfree(void *ptr) { heap_busy = 0; }
int kapi_snd2_dev_info(u32 index, SND2_DevInfo *info)
{
    if (index) return -1;
    info->kind = SND2_KIND_PCM_OUT;
    info->flags = SND2_DEV_READY | SND2_DEV_SOFT_CPU;
    return 0;
}
int __wrap_v86_boot_prepare(const char *path, const char *second, struct v86_context *ctx)
{
    require(cr3 == 0x4000 && !v86_session.open, "boot address space");
    ++opens;
    v86_session.open = 1;
    return 0;
}
int v86_mem_enable_slices(void) { return 0; }
void v86_mem_suspend(void) { require(cr3 == 0x4000, "suspend master"); ++suspends; }
void v86_mem_resume(void) { require(cr3 == 0x4000, "resume master"); ++resumes; }
void v86_session_end(void) { v86_session = (struct v86_session_state){0}; }
void tss_iomap_deny(u16 port) { require(port >= 0x188 && port <= 0x18f, "trap port"); }
int sys_time_now(u32 *lo, u32 *hi) { *lo = ++now_us; *hi = 0; return 0; }

static void soft_boots(void)
{
    V86_Begin b = {.size = sizeof b, .flags = 1, .path = "tone.fdi"};
    V86_RunIn in = {.size = sizeof in, .slice_ms = 10};
    V86_Exit ex;
    SND2_FmEv ev[2];
    u32 sid, last = 0, seq;
    caller_slot.state = APP_STATE_RUNNING;
    caller_slot.parent = APP_ID_SHELL;
    caller_slot.as = &caller_as;
    v86_session.open = 0;
    slicing = 1;
    for (unsigned i = 0; i < 2; ++i) {
        require(!v86s_begin(&b, &sid) && sid != last && cr3 == 0x3000, "soft restart");
        require(v86s_begin(&b, &sid) == OS32_ERR_BUSY, "soft busy");
        write_tone = 1;
        require(!v86s_run(sid, &in, &ex) && ex.reason == 1 && cr3 == 0x3000, "slice return");
        require(v86s_fm_drain(sid, ev, 2, &seq) == 2 && seq == 2 &&
                ev[0].reg == 0x28 && ev[0].val == 0x10 && ev[0].seq == 1 &&
                ev[1].reg == 0x28 && !ev[1].val && ev[1].seq == 2, "key off sequence");
        ++tick_count;
        ++expected_ticks;
        require(!v86s_run(sid, &in, &ex) && ex.reason == 1 &&
                v86s_fm_drain(sid, ev, 2, &seq) == 0, "empty slice");
        v86_session.soft_stop = 1;
        require(!v86s_run(sid, &in, &ex) && ex.reason == 4 && ex.code == V86_EXIT_HOTKEY,
                "stop at pump");
        require(!v86s_end(sid) && !heap_busy && cr3 == 0x3000 &&
                !v86_call_pinned(), "end releases caller");
        last = sid;
    }
    require(opens == 2 && suspends == 6 && resumes == 4, "repeat boundaries");
}
static int handler(unsigned irq, void *arg)
{
    order = order * 10 + (unsigned)arg;
    return IRQ_NONE;
}
static void delivery(unsigned expected, unsigned guest)
{
    order = eoi = reflection = 0;
    fire_12();
    require(order == expected, "shared order or remaining handler");
    require(eoi == 21, "physical EOI");
    require(reflection == guest, "guest reflection");
}
static void session(unsigned expected_slave_writes)
{
    v86_session.open = 1;
    for (fault_exit = 0; fault_exit <= 1; ++fault_exit) {
        slave_writes = master_writes = 0;
        v86_run_limit(0, 1);
        require(slave_writes == expected_slave_writes, "session sound mask");
        require(master_writes == 2 && !v86_session.running, "session VSYNC mask");
    }
}
static void fixed_lines(void)
{
    eoi = reflection = mouse = 0;
    fire_2();
    require(reflection == 2 && eoi == 1, "VSYNC");
    eoi = 0;
    fire_13();
    require(mouse == 1 && eoi == 21, "mouse");
    order = 0;
    irq_dispatch(14);
    require(order == 3, "existing PCI registration");
}
void _start(void)
{
    vm = 1;
    require(!irq_register(14, handler, (void *)3, IRQ_F_SHARED), "PCI registration");
    delivery(0, 12);
    vm = 0;
    requests = 0;
    delivery(0, 0);
    require(requests == 1, "non-VM request");
    vm = 1;
    boot_probe.complete = boot_probe.contained = 1;
    for (unsigned bank = 0; bank < 2; ++bank) {
        boot_probe.bank[bank].model = OPN_PROBE_ABSENT;
        for (unsigned model = 0; model <= OPN_PROBE_PRESENT; ++model) {
            if (model == OPN_PROBE_ABSENT) continue;
            boot_probe.bank[1 - bank].model = model;
            publish_probe_irq_policy();
            require(irq_dyn_index(12) < 0, "legacy topology");
            V86_Begin b = {.size = sizeof b, .flags = V86S_FM_SOFT, .path = "tone.fdi"};
            u32 sid;
            require(v86s_begin(&b, &sid) == OS32_ERR_NOSYS && !heap_busy && !opens,
                    "physical or unknown probe");
            delivery(0, 12);
        }
    }
    fixed_lines();
    v86_pic_reset();
    struct v86_context ctx = {0};
    v86_session.open = slicing = 1;
    require(v86_run_slice(&ctx) == V86_EXIT_SLICE, "slice exit");
    require(ctx.eip == 0x108 && ctx.cs == 0x109 && ctx.esp == 0x10b &&
            ctx.ss == 0x10c && ctx.eflags == (EFLAGS_VM | EFLAGS_IF) &&
            ctx.es == 0x10d && ctx.ds == 0x10e && ctx.fs == 0x10f && ctx.gs == 0x110 &&
            ctx.edi == 0x100 && ctx.esi == 0x101 && ctx.ebp == 0x102 &&
            ctx.ebx == 0x104 && ctx.edx == 0x105 && ctx.ecx == 0x106 &&
            ctx.eax == 0x107, "slice register frame");
    check_enter(&ctx);
    for (unsigned i = 0; i < 17; ++i) {
        if (i == V86F_ESP_DUMMY) continue;
        require(entered[i] == (i == V86I_EFLAGS ? EFLAGS_VM | EFLAGS_IF : 0x100 + i),
                "resume register frame");
    }
    require(!v86_session.ticks, "masked timer counted");
    test_timer = 1;
    v86_session.soft_input = 1;
    v86_pic_set_imr(0, 0xfe);
    guest_ivt[2 * 8] = 0x123;
    guest_ivt[2 * 8 + 1] = 0x2000;
    ctx.eflags &= ~EFLAGS_IF;
    ++tick_count;
    ++expected_ticks;
    require(v86_run_slice(&ctx) == V86_EXIT_SLICE && v86_session.ticks == 1 &&
            ctx.eip == 0x123 && ctx.cs == 0x2000 && !(ctx.eflags & EFLAGS_IF) &&
            ctx.esp == sizeof guest_stack - 6 &&
            guest_stack[29] == 0x108 && guest_stack[30] == 0x109 &&
            guest_stack[31] == EFLAGS_IF, "VM timer frame");
    ctx.eflags |= EFLAGS_IF;
    ++tick_count;
    ++expected_ticks;
    require(v86_run_slice(&ctx) == V86_EXIT_SLICE && v86_session.ticks == 1,
            "in-service timer counted");
    v86_pic_out(0, 0x20);
    ctx.eip = 0x456;
    ctx.cs = 0x2100;
    check_resume_timer = 1;
    require(v86_run_slice(&ctx) == V86_EXIT_SLICE && v86_session.ticks == 2 &&
            !check_resume_timer, "foreground timer once");
    v86_pic_out(0, 0x20);
    v86_session.ticks = V86_TICK_LIMIT - 1;
    require(v86_run_slice(&ctx) == V86_EXIT_TIMEOUT &&
            v86_session.ticks == V86_TICK_LIMIT && guest_stack[29] == 0x108 &&
            guest_stack[30] == 0x109 && guest_stack[31] == EFLAGS_IF,
            "session slice timeout");
    v86_pic_reset();
    v86_session.soft_input = test_timer = 0;
    slicing = 0;
    session(2);
    boot_probe.bank[0].model = boot_probe.bank[1].model = OPN_PROBE_ABSENT;
    publish_probe_irq_policy();
    require(slave_mask & 16, "initial IRQ12 mask");
    require(irq_dyn_index(12) >= 0, "both absent topology");
    fixed_lines();
    session(0);
    delivery(0, 0);
    for (unsigned first = 1; first <= 2; ++first) {
        unsigned second = 3 - first;
        require(!irq_register(12, handler, (void *)first, IRQ_F_SHARED), "first register");
        require(!irq_register(12, handler, (void *)second, IRQ_F_SHARED), "second register");
        session(0);
        delivery(first * 10 + second, 0);
        require(!irq_unregister(12, handler, (void *)first), "first unregister");
        require(!(slave_mask & 16), "remaining handler masked");
        delivery(second, 0);
        require(!irq_unregister(12, handler, (void *)second), "second unregister");
        require(slave_mask & 16, "empty line unmasked");
        delivery(0, 0);
    }
    soft_boots();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0));
}
