/* Boot composition: production load/init, LGY attach, both adapters and NIC.
 * Module relocation and device I/O are seams; no seam registers or binds NICs. */
#define net_lgy98_adapter attach_unit_adapter
#define _start attach_unit_start
#define tick_register attach_tick_register
#define net_adapter_register attach_registration
#define net_start attach_start
#define link_init attach_link_init
#include "hostsvc_lgy98_attach_host.c"
#undef net_lgy98_adapter
#undef _start
#undef tick_register
#undef net_adapter_register
#undef net_start
#undef link_init
int tick_register(unsigned int slot, tick_fn fn);
int net_adapter_register(enum NetNicId id, const struct NetAdapter *adapter);
int net_start(enum NetNicId id, const u8 peer[6]);
void link_init(const u8 mac[ETH_ALEN]);
#include "drivers/net/e82557/lan_module.h"
#include "drivers/net/e82557/e82557.h"
#include "drivers/net/e82557/e82557_regs.h"
#include "kernel/module/module.h"
#include "bootinfo.h"
#include "io.h"
extern const struct NetAdapter net_lgy98_adapter;
void *kmemset(void *dst, int value, u32 n)
{ u8 *p = dst; for (u32 i = 0; i < n; i++) p[i] = (u8)value; return dst; }

static int present, loaded, link_ticks, link_registrations, selected, module_error;
static struct module_record record;
static struct bootinfo bi;
static int hw_state(void) { return present ? E82557_RUNNING : E82557_OFF; }
static void hw_mac(u8 *mac) { for (int i = 0; i < 6; i++) mac[i] = (u8)(16 + i); }
static int hw_send(const void *buf, u32 len) { (void)buf; (void)len; return 0; }
static int hw_recv(void *buf, u32 cap, u32 *len)
{ (void)buf; (void)cap; *len = 0; return OS32_ERR_AGAIN; }
static u32 hw_free(void) { return 4; }
static int hw_probe(const struct pci_dev *dev) { (void)dev; return 0; }
const struct pci_driver e82557_driver = {.probe = hw_probe};
int lan_module_quiet(void) { return 1; }
int lan_module_release(void) { return 1; }
#define e82557_state hw_state
#define e82557_get_mac hw_mac
#define e82557_send hw_send
#define e82557_recv hw_recv
#define e82557_rx_free hw_free
int netcd_register(const char *alias) { (void)alias; return 0; }
#include LAN_BODY_SOURCE
#undef e82557_state
#undef e82557_get_mac
#undef e82557_send
#undef e82557_recv
#undef e82557_rx_free

const struct bootinfo *bootinfo_get(void) { return &bi; }
const u32 lan_kexports[] = {0};
const struct module_symbol_contract lan_import_contracts[] = {{0}};
const struct module_symbol_contract lan_export_contracts[] = {{0}};
/* Loader seam publishes the actual body API; relocation/lifecycle have their
 * own runtime suite. The production lan_module_load does the registration. */
int module_load(const struct module_request *req, struct module_handle *out)
{ (void)req; *out = (struct module_handle){0}; return module_error; }
static int start_body(struct module_handle h);
#define module_start start_body
#include LAN_RESIDENT_SOURCE
#undef module_start
static int start_body(struct module_handle h)
{
    (void)h;
    int rc = lan_module_init(&candidate);
    candidate_valid = !rc;
    loaded = !rc;
    record.state = MODULE_RUNNING;
    return rc;
}
struct module_record *module_record_get(struct module_handle h) { (void)h; return &record; }
const u32 *module_exports(struct module_handle h) { (void)h; return loaded ? lan_kexports : 0; }
int module_ref_acquire(struct module_handle h, u32 owner, enum module_ref_kind kind, u32 addr)
{
    (void)h; (void)owner; (void)addr;
    if (kind != MODULE_REF_ACTIVE) return 0;
    record.active_refs++;
    return 1;
}
int module_ref_release(struct module_handle h, enum module_ref_kind kind)
{
    (void)h;
    if (kind != MODULE_REF_ACTIVE || !record.active_refs) return 0;
    record.active_refs--;
    return 1;
}
int tick_register(unsigned int slot, tick_fn fn)
{
    if (slot == PC98_TICK_LINK) {
        if (fn != net_tick) return TICK_ERR_INVAL;
        link_registrations++;
    }
    return 0;
}
void link_init(const u8 mac[ETH_ALEN]) { link_inits++; selected = mac[0] == 16 ? 1 : 2; }
void link_tick(void) { link_ticks++; }
void net_rx_reset(u16 protocol) { (void)protocol; }
void net_rx_demux(const struct NetNicOps *ops, const u8 *mac, const u8 *peer)
{ (void)ops; (void)mac; (void)peer; }
unsigned int ne2k_rx_ring_free_pages(void) { return 8; }
unsigned int ne2k_rx_queue_free(void) { return 4; }
unsigned int boot_irq_save(void) { return 0; }
void boot_irq_restore(unsigned int flags) { (void)flags; }

static int boot_run(int mode)
{
    u8 mac[6];
    present = mode == 1 || mode == 3;
    module_error = mode == 4 ? MODULE_INIT_FAILED : 0;
    /* Same order as kernel_boot_memory -> BOOT_NIC_LEGACY. */
    if (lan_module_load() != module_error) return 20;
    if (mode != 1 && lgy98_attach(LGY98_BASE_DEFAULT, LGY98_INT0_IRQ, 0)) return 21;
    if (net_mac(mac) || selected != (present ? 1 : 2)) return 22;
    if (mac[0] != (present ? 16 : 0)) return 23;
    if (link_inits != 1 || link_registrations != 1) return 24;
    if (record.active_refs != (present ? 1u : 0u)) return 25;
    net_tick();
    if (link_ticks != 1) return 26;
    net_unbind();
    if (record.active_refs || net_mac(mac) != OS32_ERR_HOSTDOWN) return 27;
    return 0;
}
void _start(void)
{
    u8 mode;
    int n;
    __asm__ volatile("int $0x80" : "=a"(n) : "0"(3), "b"(0), "c"(&mode), "d"(1) : "memory");
    int rc = n == 1 ? boot_run(mode) : 99;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
