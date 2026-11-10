/* Freestanding ILP32: fake bus only, byte order at the bus boundary. */
#include "mpic.h"
#include "ppc_trap.h"
#define BASE 0xfe340000U
struct event { u32 off, value; int write; };
static struct event events[2048];
static unsigned used, stage, little, pending, endless, delivered;
static u32 feature;
static void (*external)(struct ppc_frame *);
static void finish(unsigned code) __attribute__((noreturn));
static void finish(unsigned code)
{
    unsigned mark[2]={0x4d504943,code};
    __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(mark),"d"(sizeof(mark)) : "memory");
    __asm__ volatile("int $0x80" : : "a"(1),"b"(code) : "memory");
    __builtin_unreachable();
}
static void check(int ok) { if (!ok) finish(stage); }
static u32 swap(u32 x) { return __builtin_bswap32(x); }
u32 pm_mpic_bus_read(u32 address, int le)
{
    u32 off=address-BASE, value=0;
    check(used<2048 && off<PM_MPIC_WINDOW_MAX && !(off&3));
    if (off==0x1000) value=feature;
    else if (off==0x1020) value=0x1234;
    else if (off==0x200a0) {
        value=endless ? 3 : pending ? (--pending,PM_MPIC_IPI_VECTOR) : 255;
    } else check(0);
    events[used++]=(struct event){off,value,0};
    return le==(int)little ? value : swap(value);
}
void pm_mpic_bus_write(u32 address,u32 value,int le)
{
    u32 off=address-BASE;
    check(used<2048 && off<PM_MPIC_WINDOW_MAX && !(off&3));
    events[used++]=(struct event){off,le==(int)little ? value : swap(value),1};
}
void ppc_trap_set_handler(u32 vector,void (*handler)(struct ppc_frame *))
{ check(vector==0x500); external=handler; }
static void expect(unsigned at,u32 off,u32 value,int write)
{
    check(at<used && events[at].off==off && events[at].value==value && events[at].write==write);
}
static void handler(struct ppc_frame *f)
{
    check(f->vector==0x500);
    check(events[used-1].off==0x200a0); /* handler precedes EOI */
    ++delivered;
}
void _start(void)
{
    for (little=0;little<2;++little) {
        stage=10; used=0; feature=3U<<16; pending=2;
        int order=little ? PM_MPIC_MMIO_LE : PM_MPIC_MMIO_BE;
        check(pm_mpic_init(BASE,0x40000,order)==0);
        stage=11;
        expect(0,0x1000,feature,0); expect(1,0x20080,15,1);
        for (unsigned i=0;i<4;++i) {
            expect(2+2*i,0x10000+32*i,0x80010000|i,1);
            expect(3+2*i,0x10010+32*i,1,1);
        }
        expect(10,0x1020,0x1234,0); expect(11,0x1020,0x20001234,1);
        expect(12,0x10e0,255,1);
        expect(13,0x200a0,254,0); expect(14,0x200b0,0,1);
        expect(15,0x200a0,254,0); expect(16,0x200b0,0,1);
        expect(17,0x200a0,255,0);
        stage=12; expect(18,0x10a0,0x100fe,1);
        stage=13; expect(19,0x20080,0,1);
        check(used==20 && pm_mpic_source_count()==4 && external==pm_mpic_external);
        struct ppc_frame frame={.vector=0x500};
        stage=20; used=0; external(&frame);
        check(used==1); expect(0,0x200a0,255,0);
        stage=21; used=0; pending=1; delivered=0;
        pm_mpic_set_ipi_handler(handler); external(&frame);
        check(delivered==1 && used==2); expect(1,0x200b0,0,1);
        struct irq_claim claim;
        check(platform_irq_claim(2,&claim));
        check(!platform_irq_claim(1,&claim));
        used=0; platform_irq_complete(&claim); platform_irq_complete(&claim);
        check(used==1); expect(0,0x200b0,0,1);
        check(!platform_irq_claim(255,&claim) && !platform_irq_claim(128,&claim));
        stage=22; used=0;
        check(pm_mpic_send_self_ipi(0) && !pm_mpic_send_self_ipi(1));
        check(used==1); expect(0,0x20040,1,1);
        stage=30; used=0;
        check(pm_mpic_init(BASE,0x200b3,order)==PM_MPIC_WINDOW && !used);
        check(pm_mpic_init(BASE+4,0x40000,order)==PM_MPIC_WINDOW && !used);
        check(pm_mpic_init(BASE,0x40000,99)==PM_MPIC_ORDER && !used);
        stage=31; feature=128U<<16;
        check(pm_mpic_init(BASE,0x40000,order)==PM_MPIC_SOURCES && used==1);
        check(!pm_mpic_send_self_ipi(0));
        stage=32; used=0; feature=3U<<16; endless=1;
        check(pm_mpic_init(BASE,0x40000,order)==PM_MPIC_DRAIN);
        check(used==13+2*PM_MPIC_DRAIN_LIMIT);
        check(!pm_mpic_send_self_ipi(0)); endless=0;
    }
    finish(0);
}
