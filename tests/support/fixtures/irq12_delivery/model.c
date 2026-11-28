#include "idt.h"
#include "sys/platform/pc98/legacy/v86_mem.h"
#include "sys/platform/pc98/legacy/v86.h"
#include "ksetjmp.h"
#include "sys/dev/sound/fm/fm_module.c"

extern void fire_2(void), fire_12(void), fire_13(void);
volatile u32 kctx_irq_depth, tick_count;
unsigned vm, reflection, mouse, order, eoi, slave_mask = 255;
static unsigned slave_writes, master_writes;
static int fault_exit;
static unsigned requests;
struct v86_session_state v86_session;
u32 tss_get_esp0(void) { return 0; }
void tss_set_esp0(u32 esp0) {}
int exec_setjmp(k_jmpbuf buf) { return 0; }
void v86_enter(const struct v86_context *ctx)
{
    if (fault_exit) v86_runtime_end();
}
void kprintf(u8 attr, const char *fmt, ...) {}
void input_work_invalidate_locked(void) {}
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
            delivery(0, 12);
        }
    }
    fixed_lines();
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
    __asm__ volatile("int $0x80" :: "a"(1), "b"(0));
}
