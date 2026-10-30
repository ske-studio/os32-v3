#include "../../../kernel/irq/tick.h"
#include "tick_order.h"
_Static_assert(PC98_TICK_COUNT <= TICK_SLOT_COUNT, "PC98 tick slots exceed table");
/* T5c-4: resident PCI/session facade using the T5c-0 reference record. */
#include "lan_module.h"
#include "lan_imports.h"
#include "pci_bind_contract.h"
#include "pci_bind.h"
#include "bootinfo.h"
#include "config.h"
#include "os32_generations.h"
#include "../../../kernel/module/module.h"
#include "kstring.h"
#include "kprintf.h"
#include "io.h"

extern const u32 lan_kexports[];
extern const struct module_symbol_contract lan_import_contracts[];
extern const struct module_symbol_contract lan_export_contracts[];
static struct module_handle lan_handle;
static LanModuleApi candidate;
static int candidate_valid, quiet_done;
static int pci_bound;
static void (*tick_target)(void);
static irq_handler_fn irq_target;
static unsigned int irq_line;

/* Existing sessions can finish after an abort isolated their live owner. */
static int lan_callable(void)
{
    struct module_record *r = module_record_get(lan_handle);
    return candidate_valid && r && (r->state == MODULE_RUNNING || r->active_refs);
}
int lan_module_session_acquire(void)
{
    struct module_record *r = module_record_get(lan_handle);
    return module_exports(lan_handle) &&
        module_ref_acquire(lan_handle, r->owner, MODULE_REF_ACTIVE, 0);
}
void lan_module_session_release(void)
{
    module_ref_release(lan_handle, MODULE_REF_ACTIVE);
}
void lan_module_trace(u32 stage)
{
    if (kdebug.ops.nic_trace) kdebug.ops.nic_trace(stage);
}
void e82557_tick(void) { if (tick_target) tick_target(); }
int e82557_send(const void *buf, u32 len)
{ return lan_callable() ? candidate.send(buf, len) : OS32_ERR_NOSYS; }
int e82557_recv(void *buf, u32 cap, u32 *len)
{
    if (lan_callable()) return candidate.recv(buf, cap, len);
    if (len) *len = 0;
    return OS32_ERR_NOSYS;
}
void e82557_get_mac(u8 *mac)
{
    if (lan_callable()) candidate.mac(mac);
    else if (mac) kmemset(mac, 0, SFS_LAN_MAC_LEN);
}
int e82557_state(void)
{ return lan_callable() ? candidate.state() : E82557_OFF; }
u32 e82557_rx_free(void)
{ return lan_callable() ? candidate.rx_free() : 0; }
int e82557_rx_ready(void)
{ return lan_callable() ? candidate.rx_ready() : 0; }
int e82557_loopback(int on)
{ return lan_callable() ? candidate.loopback(on) : OS32_ERR_NOSYS; }
u32 e82557_stat(u32 idx)
{ return lan_callable() ? candidate.stat(idx) : 0; }
void sfs_lan_init(SfsLan *lan, const SfsLanNic *nic, const SfsIo *clock, void *ctx)
{ if (lan_callable()) candidate.lan_init(lan, nic, clock, ctx); }
int sfs_lan_set_peer(SfsLan *lan, const char *mac)
{ return lan_callable() ? candidate.lan_peer(lan, mac) : -1; }
void sfs_lan_reset(SfsLan *lan)
{ if (lan_callable()) candidate.lan_reset(lan); }
static int lan_put(void *ctx, const u8 *buf, u32 n)
{ return lan_callable() ? candidate.io.put(ctx, buf, n) : -1; }
static int lan_get(void *ctx)
{ return lan_callable() ? candidate.io.get(ctx) : -1; }
static u32 lan_now(void *ctx)
{ return lan_callable() ? candidate.io.now(ctx) : 0; }
static void lan_idle(void *ctx)
{ if (lan_callable()) candidate.io.idle(ctx); }
static int lan_can_wait(void *ctx)
{ return lan_callable() ? candidate.io.can_wait(ctx) : 0; }
const SfsIo sfs_lan_io = {lan_put, lan_get, lan_now, lan_idle, lan_can_wait};

int lan_module_irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags)
{
    struct module_record *r = module_record_get(lan_handle);
    if (arg || irq_target || !r) return IRQ_ERR_INVAL;
    unsigned int saved = irq_save();
    int rc = IRQ_ERR_BUSY;
    if (module_ref_acquire(lan_handle, r->owner, MODULE_REF_IRQ, (u32)(uptr)fn)) {
        rc = irq_register(irq, fn, arg, flags);
        if (rc) module_ref_release(lan_handle, MODULE_REF_IRQ);
        else { irq_target = fn; irq_line = irq; }
    }
    irq_restore(saved);
    return rc;
}
int lan_module_irq_unregister(unsigned int irq, irq_handler_fn fn, void *arg)
{
    if (irq != irq_line || arg || !irq_target || irq_target != fn) return IRQ_ERR_INVAL;
    unsigned int saved = irq_save();
    int rc = irq_unregister(irq, fn, arg);
    if (!rc) {
        irq_target = 0;
        module_ref_release(lan_handle, MODULE_REF_IRQ);
    }
    irq_restore(saved);
    return rc;
}
int lan_module_dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)
{
    struct module_record *r = module_record_get(lan_handle);
    if (!r || !module_ref_acquire(lan_handle, r->owner, MODULE_REF_DMA, 0))
        return DMA_POOL_ERR_STATE;
    int rc = dma_alloc(size, align, limit, out);
    if (rc) module_ref_release(lan_handle, MODULE_REF_DMA);
    return rc;
}
int lan_module_dma_free(const struct dma_buf *buf)
{
    int rc = dma_free(buf);
    if (!rc) module_ref_release(lan_handle, MODULE_REF_DMA);
    return rc;
}
static void lan_detach_tick(void)
{
    unsigned int flags = irq_save();
    if (tick_target) {
        tick_target = 0;
        module_ref_release(lan_handle, MODULE_REF_TICK);
    }
    irq_restore(flags);
}
int lan_module_probe(const struct pci_dev *dev)
{
    struct module_record *r = module_record_get(lan_handle);
    if (!candidate_valid || !r || (r->state != MODULE_STARTING && r->state != MODULE_RUNNING))
        return r && r->state == MODULE_QUARANTINED ? PCI_PROBE_QUARANTINE : PCI_PROBE_DECLINE;
    /* A single NIC body cannot be rebound over a live or quarantined device. */
    if (r->registration_refs) return candidate.state() == E82557_QUARANTINED ?
        PCI_PROBE_QUARANTINE : PCI_PROBE_DECLINE;
    if (!module_ref_acquire(lan_handle, r->owner, MODULE_REF_REGISTRATION, 0))
        return PCI_PROBE_DECLINE;
    unsigned int flags = irq_save();
    int acquired = module_ref_acquire(lan_handle, r->owner, MODULE_REF_TICK,
                                      (u32)(uptr)candidate.tick);
    if (acquired) tick_target = candidate.tick;
    irq_restore(flags);
    if (!acquired) {
        module_ref_release(lan_handle, MODULE_REF_REGISTRATION);
        return PCI_PROBE_DECLINE;
    }
    int rc = candidate.probe(dev);
    if (rc == PCI_PROBE_DECLINE) {
        lan_detach_tick();
        if (r->irq_refs || r->dma_refs) return PCI_PROBE_QUARANTINE;
        module_ref_release(lan_handle, MODULE_REF_REGISTRATION);
    }
    return rc;
}
static int lan_start_module(struct module_record *r, u32 entry, u32 *exports)
{
    (void)exports;
    int (*init)(LanModuleApi *) = (int (*)(LanModuleApi *))(uptr)entry;
    int rc = init(&candidate);
    u32 entries[LAN_MODULE_API_SLOTS];
    STATIC_ASSERT(sizeof(candidate) == sizeof(entries), lan_facade_size);
    kmemcpy(entries, &candidate, sizeof(entries));
    for (u32 i = 0; i < LAN_MODULE_API_SLOTS; i++)
        if (entries[i] < r->text || entries[i] - r->text >= r->text_size)
            return MODULE_INIT_FAILED;
    candidate_valid = 1;
    if (rc) return rc;
    const struct pci_driver *const drivers[] = {platform_lan_driver()};
    pci_bound = 1;
    pci_bind_all(drivers, sizeof(drivers) / sizeof(drivers[0]));
    return pci_quarantined() ? MODULE_INIT_FAILED : 0;
}
static int lan_quiesce(struct module_record *r)
{
    quiet_done = !r->active_refs && candidate_valid && candidate.quiet();
    return quiet_done;
}
static int lan_detach(struct module_record *r)
{
    if (!quiet_done || r->active_refs) return 0;
    lan_detach_tick();
    int rc = !irq_target || !lan_module_irq_unregister(irq_line, irq_target, 0);
    if (rc && r->registration_refs)
        module_ref_release(lan_handle, MODULE_REF_REGISTRATION);
    return rc;
}
static int lan_stop_dma(struct module_record *r)
{
    if (!quiet_done || r->active_refs || irq_target || tick_target) return 0;
    return candidate.release() && !r->dma_refs;
}
int lan_module_load(void)
{
    if (tick_register(PC98_TICK_E82557, e82557_tick) != 0) return MODULE_INIT_FAILED;
    if (lan_handle.generation) return MODULE_BUSY;
    const struct bootinfo *bi = bootinfo_get();
    const struct module_request request = {
        .path = SYS_LAN_MOD, .name = "lan",
        .policy = {
            KAPI_VERSION, OS32_MEMORY_LAYOUT_GENERATION, LAN_MODULE_ABI_GEN,
            bi->img_crc, LAN_MODULE_OPS_VERSION, bi->img_valid,
            LAN_IMPORT_COUNT, lan_import_contracts, lan_export_contracts, MODULE_ID_LAN
        },
        .imports = lan_kexports,
        .lifecycle = {.start = lan_start_module, .quiesce = lan_quiesce,
                      .detach = lan_detach, .stop_dma = lan_stop_dma}
    };
    int rc = module_load(&request, &lan_handle);
    if (!rc) rc = module_start(lan_handle);
    if (!rc && !module_exports(lan_handle)) rc = MODULE_INIT_FAILED;
    kprintf(KLOG_ATTR, "[lan] module rc=%d pair=%08x tick=%d\n", rc, bi->img_crc, tick_target != 0);
    return rc;
}
/* Preserve a completed bind, including quarantine, when start failed later. */
void lan_module_pci_bind_fallback(void)
{
    if (!pci_bound) {
        pci_bound = 1;
        pci_bind_all(NULL, 0);
    }
}
int lan_module_abort(void) { return module_abort(lan_handle); }
