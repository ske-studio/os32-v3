/* Real mechanism #1, boot scan, recording, sizing and resource collection.
 * Only the I/O ports and interrupt state are fake. */
#include "../../drivers/pci/pci.h"
#define IO_H
static int failed;
static void output(const char *s) { u32 n=0;while(s[n])n++;__asm__ volatile("int $0x80"::"a"(4),"b"(1),"c"(s),"d"(n):"memory"); }
#define CHECK(x) do { if (!(x)) { output("FAIL " #x "\n"); failed++; } } while (0)
static void clear(void *p,u32 n) { u8 *b=p;while(n--)*b++=0; }

static u32 cfg[32][8][16], masks[32][8][16], pending[32][8];
static u32 writes[32][8][16], address_port, irq_state=1, transaction_state;
static u32 devno, fnno, regno;
static unsigned int irq_save(void) { u32 old=irq_state;irq_state=0;return old; }
static void irq_restore(unsigned int f)
{
    /* Every probe must be restored before the outer critical section ends. */
    if(f) CHECK(!transaction_state);
    irq_state=f;
}
static void select_cfg(void)
{
    CHECK(!irq_state && ((address_port>>16)&255)==0);
    devno=(address_port>>11)&31;fnno=(address_port>>8)&7;regno=(address_port&255)/4;
    CHECK(regno<16);
}
static unsigned long inpd(unsigned int port)
{
    if(port==PCI_CFG_ADDR_PORT)return address_port;
    CHECK(port==PCI_CFG_DATA_PORT);select_cfg();
    return cfg[devno][fnno][regno];
}
static void outpd(unsigned int port,unsigned long v)
{
    if(port==PCI_CFG_ADDR_PORT){address_port=v;return;}
    CHECK(port==PCI_CFG_DATA_PORT);select_cfg();
    writes[devno][fnno][regno]++;
    if(v==U32_MAX || v==0xfffffffeU) {
        CHECK(regno>=4);
        if(regno==12 || regno==14)CHECK(!(v&1));
        else CHECK(!(cfg[devno][fnno][1]&3));
        pending[devno][fnno]|=1U<<regno;transaction_state++;
        cfg[devno][fnno][regno]=masks[devno][fnno][regno];
    } else {
        if(pending[devno][fnno]&(1U<<regno))transaction_state--;
        pending[devno][fnno]&=~(1U<<regno);
        cfg[devno][fnno][regno]=v;
    }
}
static void outpw(unsigned int port,unsigned int v)
{
    CHECK(port==PCI_CFG_DATA_PORT);select_cfg();CHECK(regno==1);
    if(v&3)CHECK(!pending[devno][fnno]);
    cfg[devno][fnno][1]=(cfg[devno][fnno][1]&0xffff0000U)|v;
}
void __cdecl kprintf(u8 attr,const char *fmt,...) { (void)attr;(void)fmt; }
#include "../../drivers/pci/pci_decode.c"
#include "../../drivers/pci/pci.c"
#include "../../drivers/pci/pci_barsize.c"
u32 recover_gf_dev(void) { return 13; }
#define GFINIT_RESOURCES_HOST 1
#include "../../kdebug/gfinit_resources.c"
static void setup(void)
{
    clear(cfg,sizeof(cfg));clear(masks,sizeof(masks));
    clear(writes,sizeof(writes));clear(pending,sizeof(pending));
    for(u32 d=0;d<32;d++)for(u32 f=0;f<8;f++)cfg[d][f][0]=U32_MAX;
    cfg[13][0][0]=GF_RES_GPU_ID;
}
static void endpoint(u32 d,u32 f,u32 base)
{
    cfg[d][f][0]=0x000f1000;cfg[d][f][1]=0xa5a50002;
    cfg[d][f][4]=base;masks[d][f][4]=0xfffff000;
}
void _start(void)
{
    struct recover_range ranges[RECOVER_RANGE_MAX];u32 n;
    struct pci_bar_resource r;struct pci_dev pub;
    setup();endpoint(2,0,0x20411000);endpoint(2,7,0x20412000);
    cfg[2][0][3]=PCI_HDR_MULTIFUNCTION<<16;
    CHECK(pci_init()==3 && irq_state==1);
    CHECK(!pci_bar_get(0,2,7,0x10,&r) && r.len==0x1000);
    CHECK(!gfinit_resources(ranges,&n) && n==2);
    CHECK(ranges[0].base==0x20411000 && ranges[1].base==0x20412000);
    /* Real rescan must remove stale measurements, after a device disappears. */
    setup();CHECK(pci_init()==1);
    CHECK(pci_bar_get(0,2,7,0x10,&r)<0);
    /* Aliased single-function config must not be counted eight times. */
    setup();for(u32 f=0;f<8;f++)endpoint(8,f,0x20800000);
    endpoint(3,7,0x23000000); /* absent fn0 */
    CHECK(pci_init()==2);
    CHECK(!gfinit_resources(ranges,&n) && n==1);
    CHECK(!writes[8][1][4] && !writes[3][7][4]);
    /* Overflow functions still undergo sizing even without cache slots. */
    setup();for(u32 d=0;d<6;d++)for(u32 f=0;f<8;f++) {
        endpoint(d,f,0x20000000+d*0x10000+f*0x1000);
        cfg[d][0][3]=PCI_HDR_MULTIFUNCTION<<16;
    }
    CHECK(pci_init()==PCI_MAX_DEVS && g_pci_seen==49);
    CHECK(writes[5][7][4]==2 && cfg[5][7][4]==0x20057000);
    /* ROM disabled: no writes; enabled: size and restore. */
    setup();endpoint(2,0,0x20411000);cfg[2][0][12]=0x26000000;masks[2][0][12]=0xffff8000;
    CHECK(pci_init()==2 && !writes[2][0][12]);
    cfg[2][0][12]|=1;CHECK(pci_init()==2);
    CHECK(writes[2][0][12]==2 && cfg[2][0][12]==0x26000001);
    CHECK(!pci_bar_get(0,2,0,0x30,&r) && r.len==0x8000);
    /* Each IDE channel independently selects compatibility/native mode. */
    for(u32 mode=0;mode<4;mode++) {
        setup();endpoint(2,0,0x20411000);
        u32 progif=(mode&1)|((mode&2)<<1);
        cfg[2][0][2]=0x01010000|(progif<<8);
        CHECK(pci_init()==2);
        for(u32 b=0;b<6;b++)CHECK(writes[2][0][4+b]==
            (b>=4 || (progif&(b<2?1:4)) ? 2U:0U));
    }
    setup();endpoint(2,0,0x20411000);cfg[2][0][3]=1U<<16;
    cfg[2][0][6]=0;cfg[2][0][8]=0x21f02000;
    CHECK(pci_init()==2 && !pci_get(0,&pub));
    for(u32 b=2;b<6;b++)CHECK(pub.bar[b]==0 && !writes[2][0][4+b]);
    CHECK(irq_state==1 && !transaction_state);
    output(failed?"PCI scan FAIL\n":"PCI scan PASS\n");
    __asm__ volatile("int $0x80"::"a"(1),"b"(!!failed):"memory");for(;;){}
}
