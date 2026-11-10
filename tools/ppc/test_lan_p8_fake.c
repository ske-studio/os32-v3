#include "lan_boot.h"
static u32 events[128], n, addr, last_addr, id = 0x0021106b, cmd = 0xa5a50004, sticky;
static void event(u32 x) { events[n++] = x; }
u32 ppc_irq_save(void) { event(1); return 42; }
void ppc_irq_restore(u32 f) { event(f == 42 ? 7 : 99); }
void pm_pci_mmio_write32(u32 va, u32 v)
{
    v = __builtin_bswap32(v);
    if (va == 0x1000) { addr = v; if (v) last_addr = v; event(v ? 2 : 5); }
    else { event(va == 0x2004 ? 4 : 40); if (!(v & 0xffff0000)) cmd = (cmd & 0xffff0000) | (v & 0xffff) | sticky; else cmd &= ~(v & 0xffff0000); }
}
u32 pm_pci_mmio_read32(u32 va)
{
    if (va == 0x1000) { event(addr ? 3 : 6); return __builtin_bswap32(addr); }
    event(va == 0x2004 ? 4 : 40);
    return __builtin_bswap32((addr & 0xfcu) == 4 ? cmd : id);
}
static void die(int code) { __asm__ volatile("int $0x80" : : "a"(1), "b"(code)); __builtin_unreachable(); }
static void emit(const char *s, u32 len) { __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(len) : "memory"); }
#define CHECK(x, c) do { emit("reach " #c "\n", sizeof("reach " #c "\n")-1); if (!(x)) { emit("fail " #c "\n", sizeof("fail " #c "\n")-1); die(c); } } while (0)
void _start(void)
{
    struct pm_pci p = {0x1000, 0x2000, 0, 2, 11u << 11};
    struct pm_gmac_boot b = {0}; u32 v;
    CHECK(pm_pci_read32(&p,p.target_bdf,4,&v)==0 && v==cmd, 10);
    CHECK(last_addr == ((1u << 11) | 4) && n==7 && events[0]==1 && events[1]==2 && events[2]==3 && events[3]==4 && events[4]==5 && events[5]==6 && events[6]==7, 11);
    n=0; CHECK(pm_pci_write32(&p,p.target_bdf,4,0xffff0000)==0 && cmd==0xa5a50000, 12);
    n=0; CHECK(pm_pci_read32(&p,p.target_bdf+0x100,0,&v)==LAN_EINVAL && pm_pci_read32(&p,p.target_bdf,256,&v)==LAN_EINVAL && pm_pci_read32(&p,p.target_bdf,2,&v)==LAN_EINVAL && n==0, 13);
    p.target_bdf=10u<<11; CHECK(pm_pci_read32(&p,p.target_bdf,0,&v)==LAN_EINVAL && n==0,19);
    p.target_bdf=3u<<16; CHECK(pm_pci_read32(&p,p.target_bdf,0,&v)==LAN_EINVAL && n==0,20);
    p.bus_first=1; p.target_bdf=11u<<11; CHECK(pm_pci_read32(&p,p.target_bdf,0,&v)==LAN_EINVAL && n==0,21); p.bus_first=0;
    p.target_bdf=(1u<<16)|(2u<<11); n=0; CHECK(pm_pci_read32(&p,p.target_bdf,0,&v)==0 && last_addr == ((1u<<16)|(2u<<11)|1u),14);
    p.target_bdf=11u<<11; b.node=1; b.profile=PM_GMAC_PROFILE_UNINORTH; b.bdf=p.target_bdf; b.vendor_device=id;
    pm_gmac_quiesce_setup(0,&p); n=0; CHECK(pm_gmac_early_quiesce()==0 && n==0,15);
    pm_gmac_quiesce_setup(&b,&p); id=0; n=0; CHECK(pm_gmac_early_quiesce()==0 && n==7,16);
    id=b.vendor_device; cmd=0xa5a50004; n=0; CHECK(pm_gmac_early_quiesce()==0 && !(cmd&4),17);
    sticky=4; cmd|=4; n=0; CHECK(pm_gmac_early_quiesce()==LAN_EIO,18);
    die(0);
}
