/* Exercise the real LGY attach path; hardware and shared-owner seams only. */
#include "hostsvc_fixture.h"
#include "drivers/net/ne2000/lgy98.h"
#include "drivers/net/ne2000/ne2000.h"
#include "kernel/irq/tick.h"
#include "platform/pc98/tick_order.h"
#include "kernel/irq/irq.h"
#include "net/link.h"
#include "kprintf.h"
#include "kstring.h"
#include "os32/os32_kapi_shared.h"

static int init_rc, register_rc, irq_rc, tick_rc;
static int initialized, registrations, link_inits, irq_enables, bad;
static int rx_calls, tx_calls, rx_pending, busy;
static tick_fn reflect_tick;
const struct NetAdapter net_lgy98_adapter = {0, 0};

int tick_register(unsigned int slot, tick_fn fn)
{
    if (slot != PC98_TICK_LGY98 || fn != lgy98_tick) bad = 1;
    reflect_tick = fn;
    return tick_rc;
}
int net_adapter_register(enum NetNicId id, const struct NetAdapter *adapter)
{
    registrations++;
    if (!initialized || id != NET_NIC_LGY98 || adapter != &net_lgy98_adapter) bad = 1;
    return register_rc;
}
int net_start(enum NetNicId id, const u8 peer[6])
{
    if (id != NET_NIC_AUTO || peer || registrations != 1 || register_rc) bad = 1;
    return 0;
}
int ne2k_init(const struct ne2k_config *cfg)
{
    if (cfg->reg_base != LGY98_BASE_DEFAULT || cfg->irq != LGY98_INT0_IRQ) bad = 1;
    initialized = !init_rc;
    return init_rc;
}
int irq_register(unsigned int irq, irq_handler_fn fn, void *arg, unsigned int flags)
{
    if (!initialized || irq != LGY98_INT0_IRQ || fn != ne2k_irq_shared ||
        arg || flags != IRQ_F_SHARED) bad = 1;
    return irq_rc;
}
int ne2k_irq_shared(unsigned int irq, void *arg) { (void)irq; (void)arg; return IRQ_NONE; }
void ne2k_irq_enable(void) { irq_enables++; }
int np2_detect(void) { return 0; }
void ne2k_get_mac(u8 mac[ETH_ALEN]) { for (int i = 0; i < ETH_ALEN; i++) mac[i] = (u8)i; }
unsigned int ne2k_ram_bytes(void) { return 32768; }
void ne2k_get_stats(struct ne2k_stats *st) { *st = (struct ne2k_stats){0}; }
int ne2k_state(void) { return initialized ? NE2K_STATE_RUNNING : NE2K_STATE_STOPPED; }
int ne2k_is_busy(void) { return busy; }
int ne2k_recv(void *out, unsigned int cap, unsigned int *len)
{
    u8 *p = out;
    rx_calls++;
    if (!rx_pending) return NE2K_ERR_AGAIN;
    if (cap < ETH_HLEN) { bad = 1; return NE2K_ERR_NOSPACE; }
    rx_pending = 0;
    for (int i = 0; i < ETH_HLEN; i++) p[i] = (u8)i;
    *len = ETH_HLEN;
    return 0;
}
int ne2k_send(const void *frame, unsigned int len)
{
    const u8 *p = frame;
    tx_calls++;
    if (len != ETH_HLEN) bad = 1;
    for (int i = 0; i < ETH_ALEN; i++)
        if (p[i] != i + ETH_ALEN || p[i + ETH_ALEN] != i) bad = 1;
    return 0;
}
void *kmemcpy(void *dst, const void *src, u32 n)
{
    u8 *d = dst; const u8 *s = src;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
    return dst;
}
void kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void link_init(const u8 mac[ETH_ALEN]) { (void)mac; link_inits++; }
void link_selftest(int rounds) { (void)rounds; }
void link_l1_bulk(unsigned int count, unsigned int payload) { (void)count; (void)payload; }
void link_l2_stream(unsigned int total, unsigned int plen, unsigned int dropseq)
{ (void)total; (void)plen; (void)dropseq; }
void link_l3_service(void) { }

static void reset(void)
{
    hs_reset(0);
    init_rc = register_rc = irq_rc = tick_rc = 0;
    initialized = registrations = link_inits = irq_enables = bad = 0;
    rx_calls = tx_calls = rx_pending = busy = 0;
    reflect_tick = 0;
}
static int attach(unsigned int flags)
{
    return lgy98_attach(LGY98_BASE_DEFAULT, LGY98_INT0_IRQ, flags);
}
static int run(void)
{
    reset();
    if (attach(0) || registrations != 1 || link_inits || bad || irq_enables != 1) return 1;
    if (reflect_tick != lgy98_tick) return 2;
    reflect_tick();
    if (rx_calls || tx_calls) return 3; /* shared RX belongs to net_tick */

    reset(); init_rc = NE2K_ERR_IO;
    if (attach(0) != NE2K_ERR_IO || registrations || link_inits || irq_enables || bad) return 4;
    reset(); register_rc = OS32_ERR_BUSY;
    if (attach(0) != NE2K_ERR_IO || registrations != 1 || link_inits || irq_enables || bad) return 5;
    reset(); tick_rc = TICK_ERR_BUSY;
    if (attach(0) != NE2K_ERR_IO || initialized || registrations || bad) return 6;
    reset();
    if (lgy98_attach(0, LGY98_INT0_IRQ, 0) != NE2K_ERR_INVAL || registrations || initialized) return 7;
    if (lgy98_attach(LGY98_BASE_DEFAULT, 0, 0) != NE2K_ERR_INVAL || registrations || initialized) return 8;

    reset(); irq_rc = IRQ_ERR_BUSY;
    if (attach(0) || registrations != 1 || irq_enables || bad) return 9;
    reset();
    if (attach(LGY98_FLAG_REFLECT) || registrations || link_inits || bad || !reflect_tick) return 10;
    rx_pending = 1; busy = 1;
    reflect_tick();
    if (rx_calls || tx_calls) return 11;
    busy = 0;
    u32 reflected = lgy98_reflected;
    reflect_tick();
    if (rx_calls != 2 || tx_calls != 1 || lgy98_reflected != reflected + 1 || bad) return 12;
    return 0;
}
void _start(void)
{
    int rc = run();
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
