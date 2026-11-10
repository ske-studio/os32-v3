#include "lan_diag.c"
static struct pm_kboot_info info;
volatile u32 tick_count = 0x11223344;
static u32 enabled = 1, restored;
u32 ppc_irq_save(void) { u32 old = enabled; enabled = 0; return old; }
void ppc_irq_restore(u32 x) { enabled = x; restored++; }
const struct pm_kboot_info *ppc_boot_info(void) { return &info; }
u32 ppc_root_current(void) { return 0x1000; }
int ppc_pd_lookup_read(u32 root, u32 va, int store, int user, struct ppc_translation *out, ppc_pd_read_fn read, void *ctx) {
    (void)read; (void)ctx; if (root != 0x1000 || store || user || va != 0x1234) return 0; out->pa = 0x5678; return 1;
}
int pm_lan_copy8(u32 va, u8 *out) { (void)va; (void)out; return 0; }
int platform_clock_read_us(u32 *lo, u32 *hi) { *lo=0x99aabbcc; *hi=0x55667788; return enabled; }
static u64 tb(void *ctx) { (void)ctx; return 0x1020304050607080ULL; }
static void print(const char *s) { u32 n=0; while(s[n]) n++; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
static void die(const char *s) { print(s); __asm__ volatile("int $0x80" : : "a"(1), "b"(1)); __builtin_unreachable(); }
void _start(void) {
    struct gem g = {0}; struct pm_gmac_boot boot = {0}; const struct pm_lan_diag *d;
    const u32 sw[17] = {9,1,2, 3,4, 5,6, 7,8, 9,10, 11,12, 13,14,15,16};
    const u32 mw[8] = {0,0xf00000,0x1000000,0x20000,0x1100000,0x40000,0x400000,0x30000};
    u8 expected[256] = {0}; u32 i,j,pa;
    info.admitted_end=mw[1]; info.image_first=mw[2]; info.image_end=mw[2]+mw[3]; info.hpt_pa=mw[4]; info.hpt_bytes=mw[5]; info.tb_hz=0x12345678;
    g.dma.pa=mw[6]; g.dma.bytes=mw[7]; g.phy_id1=1; g.phy_id2=2; g.io.tb=tb;
    g.tx_packets=0x300000004ULL; g.rx_packets=0x500000006ULL; g.rx_drops=0x700000008ULL; g.uncertain_tx=0x90000000aULL;
    g.error_status=13; g.tx_status=14; g.rx_status=15; g.tx_pending=16;
    for(i=0;i<16;i++) boot.boot_id[i]=(u8)(i+1);
    for(i=0;i<6;i++) { boot.mac[i]=(u8)(0x20+i); boot.peer_mac[i]=(u8)(0x30+i); }
    d=pm_lan_diag_snapshot(&boot,&g,9,0xb0000000cULL);
    print("reached=range\n");
    if(d->peek_ok_count!=2 || d->peek_ok[0].base!=0 || d->peek_ok[0].size!=0x420000 || d->peek_ok[1].base!=0x430000 || d->peek_ok[1].size!=0xad0000) die("H:A10 read-range\n");
    print("reached=status\n");
    for(i=0;i<17;i++) if(d->status[i]!=sw[i]) die("H:status\n");
    for(i=0;i<8;i++) if(d->memory[i]!=mw[i]) die("H:memory\n");
    for(i=0;i<17;i++) for(j=0;j<4;j++) expected[i*4+j]=(u8)(sw[i]>>(24-j*8));
    for(i=0;i<8;i++) for(j=0;j<4;j++) expected[72+i*4+j]=(u8)(mw[i]>>(24-j*8));
    { const u8 clk[24]={0x11,0x22,0x33,0x44,0x12,0x34,0x56,0x78,0x10,0x20,0x30,0x40,0x50,0x60,0x70,0x80,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc}; for(i=0;i<24;i++) expected[104+i]=clk[i]; }
    for(i=0;i<16;i++) expected[128+i]=(u8)(i+1);
    for(i=0;i<6;i++) { expected[144+i]=(u8)(0x20+i); expected[150+i]=(u8)(0x30+i); }
    print("reached=diag\n");
    for(i=0;i<256;i++) if(d->diag[i]!=expected[i]) die("H:diag\n");
    print("reached=irq\n");
    if(enabled!=1 || restored!=1) die("H:irq\n");
    if(d->translate(0x1234,&pa)!=0 || pa!=0x5678 || d->translate(0x9999,&pa)!=1) die("H:translate\n");
    print("PASS\n"); __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); __builtin_unreachable();
}
