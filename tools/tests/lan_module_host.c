/* Real module runtime and resident LAN facade. PCI enumeration, device I/O,
 * and relocated body entry are explicit seams, not hardware evidence. */
#define _start unused_runtime_start
#include "module_runtime_host.c"
#undef _start
#include "io.h"
#define irq_save() 0U
#define irq_restore(f) ((void)(f))
#ifndef LAN_ADAPTER
#define LAN_ADAPTER "../../kernel/lan_module.c"
#endif
#include LAN_ADAPTER
#include "../../kernel/tick.c"
volatile u32 kctx_irq_depth;
const u32 lan_kexports[] = {0};
const struct module_symbol_contract lan_import_contracts[] = {{0}};
const struct module_symbol_contract lan_export_contracts[] = {{0}};
const struct bootinfo *bootinfo_get(void) { finish(2); }


enum { NORMAL, DECLINE, QUARANTINE, ACTIVE, PREFIX_FAIL, START_FAIL, CLEAN_DECLINE, MISSING, INIT_FAIL };
static u32 mode, ticks, probes, sends, gets, quiet_calls, release_calls;
static u32 dma_live, dma_freed, irq_live, irq_calls, cleanup_order;
static int probe_result;
static struct dma_buf ring;
static struct pci_dev nic = {.vendor = E82557_VENDOR, .device = E82557_DEVICE,
    .class = PCI_CLASS_NETWORK, .subclass = PCI_SUB_NET_ETHERNET, .irq_line = 5};
int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    CHECK(size == PAGE_SIZE && align == PAGE_SIZE && limit == DMA_PHYS_LIMIT && !dma_live);
    dma_live = 1;
    *out = (struct dma_buf){.pa = MEM_DMA_POOL_BASE, .va = &ring, .size = size};
    return 0;
}
int dma_free(const struct dma_buf *buf)
{
    CHECK(buf->pa == MEM_DMA_POOL_BASE && dma_live && !irq_live);
    CHECK(!tick_target || mode == CLEAN_DECLINE);
    dma_live = 0; dma_freed++;
    return 0;
}
int irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags)
{
    CHECK(irq == 5 && fn && !arg && flags == IRQ_F_SHARED && !irq_live);
    irq_live = 1;
    return 0;
}
int irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg)
{
    CHECK(irq == 5 && fn && !arg && irq_live);
    CHECK(cleanup_order == 1 || mode == CLEAN_DECLINE);
    irq_live = 0;
    return 0;
}
static int body_irq(unsigned int irq, void *arg)
{ CHECK(irq == 5 && !arg && irq_live); irq_calls++; return IRQ_HANDLED; }
static void body_tick(void) { ticks++; }
static int body_state(void) { return mode == QUARANTINE ? E82557_QUARANTINED : E82557_RUNNING; }
static int body_send(const void *buf, u32 len)
{ CHECK(buf && len == 1 && dma_live); sends++; return 0; }
static int body_get(void *ctx) { CHECK(ctx == &nic); gets++; return 42; }
static int body_probe(const struct pci_dev *dev)
{
    struct module_record *r = module_record_get(lan_handle);
    CHECK(dev->vendor == nic.vendor && !module_exports(lan_handle));
    CHECK(r->tick_refs == 1 && r->registration_refs == 1 && tick_target);
    probes++;
    if (mode == DECLINE) return PCI_PROBE_DECLINE;
    CHECK(!lan_module_dma_alloc(PAGE_SIZE, PAGE_SIZE, DMA_PHYS_LIMIT, &ring));
    CHECK(!lan_module_irq_register(5, body_irq, 0, IRQ_F_SHARED));
    CHECK(irq_target(5, 0) == IRQ_HANDLED);
    tick_dispatch();
    CHECK(e82557_send("x", 1) == OS32_ERR_NOSYS);
    if (mode == CLEAN_DECLINE) {
        CHECK(!lan_module_irq_unregister(5, body_irq, 0));
        CHECK(!lan_module_dma_free(&ring));
        return PCI_PROBE_DECLINE;
    }
    return mode == QUARANTINE ? PCI_PROBE_QUARANTINE : PCI_PROBE_OK;
}
static int body_quiet(void)
{
    CHECK(!module_exports(lan_handle));
    quiet_calls++; cleanup_order = 1;
    return mode != QUARANTINE;
}
static int body_release(void)
{
    CHECK(cleanup_order == 1 && !irq_target && !tick_target && !irq_live);
    CHECK(!module_record_get(lan_handle)->registration_refs);
    cleanup_order = 2; release_calls++;
    if (dma_live) CHECK(!lan_module_dma_free(&ring));
    return 1;
}
static int init_lan(LanModuleApi *out)
{
    CHECK(!module_exports(lan_handle));
    u32 entries[LAN_MODULE_API_SLOTS];
    for (u32 i = 0; i < LAN_MODULE_API_SLOTS; i++) entries[i] = (u32)(uptr)body_tick;
    kmemcpy(out, entries, sizeof(entries));
    out->probe = body_probe; out->tick = body_tick; out->state = body_state;
    out->quiet = body_quiet; out->release = body_release;
    out->send = body_send; out->io.get = body_get;
    return mode == INIT_FAIL ? MODULE_INIT_FAILED : 0;
}
int pci_count(void) { return 2; }
int pci_get(u32 idx, void *buf)
{
    struct pci_dev *out = buf;
    if (idx >= 2) return -1;
    *out = nic;
    if (idx) { out->vendor = 0x1234; out->dev = 1; out->irq_line = 7; }
    return 0;
}
#define pci_bind_all real_pci_bind_all
#include "../../drivers/pci_bind.c"
#undef pci_bind_all
#include "../../drivers/pci_bind_match.c"
static u32 bind_calls, bind_reports;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (!kstrcmp(fmt, "[pci] bind: %d bound, %d quarantined of %d\n")) bind_reports++;
}
int pci_bind_all(const struct pci_driver *const *table, int n)
{
    bind_calls++;
    int rc = real_pci_bind_all(table, n);
    struct pci_bind_info info;
    CHECK(!pci_bind_info_get(0, &info));
    probe_result = info.result == PCI_BIND_BOUND ? PCI_PROBE_OK :
        info.result == PCI_BIND_QUARANTINED ? PCI_PROBE_QUARANTINE : PCI_PROBE_DECLINE;
    return rc;
}
static u8 line_bits(unsigned int irq)
{ return irq == 5 ? PCI_LINE_BIT_QUARANTINED : PCI_LINE_BIT_STORM; }
static void check_fallback(void)
{
    struct pci_bind_info info;
    lan_module_pci_bind_fallback();
    lan_module_pci_bind_fallback();
    CHECK(bind_calls == 1 && bind_reports == 1);
    pci_bind_set_line_state_hook(line_bits);
    for (int i = 0; i < 2; i++) {
        CHECK(!pci_bind_info_get(i, &info));
        CHECK(info.result == PCI_BIND_NONE && info.reason == PCI_BIND_NO_DRIVER);
        CHECK(info.irq == (i ? 7 : 5));
        CHECK(info.line_state == (i ? PCI_LINE_STORM_MASKED : PCI_LINE_QUARANTINED));
    }
    CHECK(pci_bind_info_get(2, &info) == -1);
}
static int start_then_fail(struct module_record *r, u32 entry, u32 *ops)
{
    int rc = lan_start_module(r, entry, ops);
    return !rc && mode == START_FAIL ? MODULE_INIT_FAILED : rc;
}
static void no_refs(struct module_record *r)
{
    CHECK(!r->irq_refs && !r->tick_refs && !r->dma_refs && !r->active_refs);
    CHECK(!r->registration_refs && !r->callback_refs && !irq_target && !tick_target);
}
void _start(void)
{
    kctx_irq_depth = 1;
    CHECK(lan_module_load() == MODULE_INIT_FAILED);
    CHECK(!lan_handle.generation && !slots[PC98_TICK_E82557].fn);
    CHECK(tick_register(PC98_TICK_E82557, e82557_tick) == TICK_ERR_CTX);
    kctx_irq_depth = 0;
    /* The retry reaches the existing busy check after publishing the facade. */
    lan_handle.generation = 1;
    CHECK(lan_module_load() == MODULE_BUSY);
    lan_handle.generation = 0;
    CHECK(slots[PC98_TICK_E82557].fn == e82557_tick);
    CHECK(!tick_register(PC98_TICK_E82557, e82557_tick));
    CHECK(!tick_register(PC98_TICK_E82557, e82557_tick));
    read_input(input, sizeof(input)); read_input(file, sizeof(file));
    mode = input[1];
    kmemcpy(&imports[0].name_hash, file + 152, 4);
    kmemcpy(&exports[4].name_hash, file + 164, 4);
    exports[4].version = 1;
    struct module_request q = request();
    q.lifecycle = (struct module_lifecycle){.start = start_then_fail, .quiesce = lan_quiesce,
                                           .detach = lan_detach, .stop_dma = lan_stop_dma};
    u32 n = 7; u8 mac[SFS_LAN_MAC_LEN];
    CHECK(e82557_send("x", 1) == OS32_ERR_NOSYS && !lan_module_session_acquire());
    CHECK(e82557_recv(mac, sizeof(mac), &n) == OS32_ERR_NOSYS && !n);
    CHECK(e82557_state() == E82557_OFF && sfs_lan_io.get(&nic) == -1);
    tick_dispatch(); CHECK(!ticks);
    if (mode == MISSING) { check_fallback(); finish(0); }
    CHECK(!module_load(&q, &lan_handle));
    struct module_record *r = module_record_get(lan_handle);
    r->init = (u32)(uptr)init_lan;
    r->text = (u32)(uptr)body_irq & ~(PAGE_SIZE - 1);
    r->text_size = 16 * PAGE_SIZE;
    int rc = module_start(lan_handle);
    if (mode == INIT_FAIL) {
        CHECK(rc == MODULE_INIT_FAILED && !probes);
        check_fallback(); empty(); finish(0);
    }
    lan_module_pci_bind_fallback();
    CHECK(bind_calls == 1 && bind_reports == 1);
    CHECK(probes == 1);
    if (mode == QUARANTINE) {
        CHECK(rc == MODULE_INIT_FAILED && r->state == MODULE_QUARANTINED);
        CHECK(!module_exports(lan_handle) && !lan_module_session_acquire());
        CHECK(!returned && !retired && live[0] && mask[0] == 3);
        CHECK(r->irq_refs == 1 && r->tick_refs == 1 && r->dma_refs == 1);
        CHECK(r->registration_refs == 1 && !dma_freed && !release_calls);
        CHECK(e82557_driver.probe(&nic) == PCI_PROBE_QUARANTINE && probes == 1);
        CHECK(lan_module_abort() == MODULE_ISOLATED && !returned && !dma_freed);
        finish(0);
    }
    if (mode == PREFIX_FAIL || mode == START_FAIL) {
        CHECK(rc == MODULE_INIT_FAILED && r->state == MODULE_ABSENT);
        no_refs(r); CHECK(!dma_live && !irq_live && dma_freed == 1);
        CHECK(quiet_calls == 1 && release_calls == 1); empty(); finish(0);
    }
    CHECK(!rc && module_exports(lan_handle));
    if (mode == DECLINE || mode == CLEAN_DECLINE) {
        CHECK(probe_result == PCI_PROBE_DECLINE);
        no_refs(r); CHECK(!dma_live && !irq_live);
        CHECK(dma_freed == (mode == CLEAN_DECLINE));
        CHECK(!lan_module_abort()); empty(); finish(0);
    }
    CHECK(r->registration_refs == 1 && r->irq_refs == 1 && r->tick_refs == 1 && r->dma_refs == 1);
    CHECK(irq_calls == 1 && ticks == 1 && !sends);
    CHECK(e82557_driver.probe(&nic) == PCI_PROBE_DECLINE && probes == 1);
    CHECK(!e82557_send("x", 1) && sends == 1);
    CHECK(sfs_lan_io.get(&nic) == 42 && gets == 1);
    u32 before = returned;
    if (mode == ACTIVE) CHECK(lan_module_session_acquire() && r->active_refs == 1);
    rc = lan_module_abort();
    if (mode == ACTIVE) {
        CHECK(rc == MODULE_ISOLATED && r->state == MODULE_QUARANTINED && !quiet_calls);
        CHECK(!module_exports(lan_handle) && !lan_module_session_acquire());
        CHECK(returned == before && mask[0] == 2 && live[0] && !retired);
        CHECK(r->active_refs == 1 && irq_target && tick_target && dma_live && !dma_freed);
        /* Retained session/NIC callbacks can return before its active ref drops. */
        CHECK(!e82557_send("x", 1) && sends == 2);
        CHECK(sfs_lan_io.get(&nic) == 42 && gets == 2);
        lan_module_session_release(); CHECK(!r->active_refs);
        CHECK(e82557_send("x", 1) == OS32_ERR_NOSYS && sfs_lan_io.get(&nic) == -1);
        CHECK(e82557_driver.probe(&nic) == PCI_PROBE_QUARANTINE && probes == 1);
        CHECK(lan_module_abort() == MODULE_ISOLATED);
        CHECK(returned == before && !dma_freed && !release_calls && !retired);
    } else {
        CHECK(!rc && r->state == MODULE_ABSENT); no_refs(r);
        CHECK(!dma_live && !irq_live && dma_freed == 1 && release_calls == 1);
        tick_dispatch(); CHECK(ticks == 1);
        CHECK(e82557_send("x", 1) == OS32_ERR_NOSYS && sfs_lan_io.get(&nic) == -1);
        CHECK(!lan_module_abort()); empty();
    }
    finish(0);
}
