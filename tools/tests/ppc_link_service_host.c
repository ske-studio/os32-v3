#define KBOOT_LINK 1
#include "lan_service.c"
static int starts, registered, rx_calls, ticks, stage;
u32 link_hello_ok;
static void fail(void) { __asm__ volatile("int $0x80"::"a"(1),"b"(10)); __builtin_unreachable(); }
#define REQUIRE(x) do { if (!(x)) fail(); } while (0)
void kprintf(u8 a, const char *f, ...) { (void)a; (void)f; }
u32 kmalloc_used(void) { return 18000; }
int host_mailbox_init(void) { return 0; }
const struct NetAdapter net_gem_adapter = {0};
int net_adapter_register(enum NetNicId id, const struct NetAdapter *a) {
    REQUIRE(s.state == RUNNING && id == PM_NET_NIC_GEM && a == &net_gem_adapter);
    registered++; return 0;
}
int net_start(enum NetNicId id, const u8 p[6]) {
    REQUIRE(s.state == RUNNING && registered == 1 && id == PM_NET_NIC_GEM && !p);
    starts++; return 0;
}
void net_unbind(void) { starts--; }
void pm_rh_stage(unsigned n) { stage = (int)n; }
i32 link_host_status(i32 h,u32 *s,u32 *l,int o) { (void)h;(void)s;(void)l;(void)o;return -1; }
i32 link_host_open(const char *r, u32 n, int o) { (void)r; (void)n; (void)o; return -1; }
i32 link_host_read_stage(i32 h,u32 n,const u8 **p,int o) { (void)h;(void)n;(void)p;(void)o;return -1; }
i32 link_host_close(i32 h,int o) { (void)h;(void)o;return 0; }
void gem_enter_reset(struct gem *g) { g->state = GEM_STATE_RESETTING; }
int gem_reset_step(struct gem *g,u64 n) { (void)g;(void)n;return GEM_OK; }
int gem_mif_init(struct gem *g) { (void)g;return GEM_OK; }
int gem_phy_sample(struct gem *g,struct gem_phy_sample *p) { (void)g;(void)p;return 0; }
int gem_phy_step(struct gem *g,u64 n) { (void)g;(void)n;return 0; }
int gem_mac_enable_step(struct gem *g,u64 n) { (void)n;g->state=GEM_STATE_READY;return 0; }
int gem_ring_prepare(struct gem *g,struct gem_dma *d) { (void)g;(void)d;return 0; }
int gem_ring_publish(struct gem *g) { (void)g;return 0; }
int gem_mac_init(struct gem *g,const u8 m[6]) { (void)g;(void)m;return 0; }
int pm_gmac_dma_init(const struct pm_gmac_boot *b,struct gem_dma *d) { (void)b;(void)d;return 0; }
int gem_rx(struct gem *g,void *p,u32 n,u32 *l) { (void)g;(void)p;(void)n;(void)l;rx_calls++;return GEM_AGAIN; }
static struct pm_gmac_rh_sample sample;
const struct pm_gmac_rh_sample *pm_gmac_rh_sample(void) { return &sample; }
void _start(void) {
    static struct pm_gmac_boot b;
    s.boot=&b; s.g.io.tb_hz=1000; s.state=RESETTING;
    for (int i=0;i<4;i++) {
        pm_lan_poll(++ticks);
        REQUIRE(!starts && !registered && !rx_calls);
    }
    REQUIRE(s.state==RUNNING);
    pm_lan_poll(++ticks);
    REQUIRE(starts==1 && registered==1 && !rx_calls && !stage);
    for (int i=0;i<10;i++) pm_lan_poll(++ticks);
    REQUIRE(starts==1 && !rx_calls);
    link_hello_ok=1;pm_lan_poll(++ticks);
    REQUIRE(stage==17);
    s.g.state=GEM_STATE_RESETTING;pm_lan_poll(++ticks);
    REQUIRE(starts==0 && !link_bound && s.state==RESETTING);
    __asm__ volatile("int $0x80"::"a"(1),"b"(0)); __builtin_unreachable();
}
