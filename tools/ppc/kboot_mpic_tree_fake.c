/* Reuse B1 firmware, capture the real product snapshot, then vary raw windows.
 * Synthetic mac99/KeyLargo G4 shapes, not observed firmware dumps. */
#define entry capture_entry
#include "kboot_capture_fake.c"
#undef entry
#include "mpic_tree.h"
static struct pm_kboot_info copy;
void entry(unsigned *stack)
{
    unsigned which=0;
    if (stack[0]>1) {
        const char *p=(const char *)stack[2];
        while (*p) which=which*10+(*p++-'0');
    }
    ac=1; sc=1; mac=0; scenario=0;
    tuple(reg,1,1,0,0x8000000); rn=8;
    tuple(avail,1,1,0x4000,0x10000); an=8;
    copy=*pm_kboot_capture(0);
    struct pm_kboot_node *leaf=&copy.mpic.v[0], *m=&copy.mpic.v[1];
    struct pm_kboot_node *pci=&copy.mpic.v[2];
    unsigned expected=PM_MPIC_OK;
    u32 want=0x80040000, size=0x40000, pa=123, bytes=456;
    int order=77;
    if (which==1) { /* nonidentity CPU parent: G4-shaped bus, different PA */
        put(pci->ranges.data+12,0x90000000); want=0x90040000;
    }
    if (which==2) { /* two ranges, first does not cover the device */
        for (unsigned j=0;j<24;j++) pci->ranges.data[24+j]=pci->ranges.data[j];
        pci->ranges.bytes=48; put(pci->ranges.data+8,0x40000000);
        put(pci->ranges.data+20,0x1000);
    }
    if (which==3) { put(pci->ranges.data+8,0x90000000); expected=PM_MPIC_NO_RANGE; }
    if (which==4) { put(m->assigned.data,0x81008010); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==5) { put(leaf->reg.data+4,0); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==6) { put(leaf->reg.data,0xffff0000); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==7) { put(pci->ranges.data+12,0xffff0000); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==8) { m->ranges.present=0; expected=PM_MPIC_BAD_PROPERTY; }
    if (which==9) { m->ranges.bytes--; expected=PM_MPIC_BAD_PROPERTY; }
    if (which==10) { put(m->assigned.data,0x02008010); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==11) { put(m->assigned.data+4,1); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==12) { put(m->assigned.data+12,1); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==13) { put(leaf->reg.data+4,0x200b0); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==14) { copy.mpic.machine=PM_KBOOT_MACHINE_UNKNOWN; expected=PM_MPIC_BAD_ORDER; }
    if (which==15) { /* decoy BAR before BAR0, order is not correspondence */
        for (unsigned j=0;j<20;j++) m->assigned.data[20+j]=m->assigned.data[j];
        m->assigned.bytes=40; put(m->assigned.data,0x82008014);
        put(m->assigned.data+8,0x70000000);
    }
    if (which==16) { put(m->assigned.data,0x82008014); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==17) { put(m->assigned.data+16,0); expected=PM_MPIC_BAD_ADDRESS; }
    if (which==18) { /* high CPU address */
        copy.mpic.v[3].ac=2; pci->ranges.bytes=28;
        put(pci->ranges.data+12,1); put(pci->ranges.data+16,0x80000000);
        put(pci->ranges.data+20,0); put(pci->ranges.data+24,0x10000000);
        expected=PM_MPIC_BAD_ADDRESS;
    }
    if (which==19) { copy.mpic.count=0; expected=PM_MPIC_BAD_TREE; }
    if (which==20) { /* overlapping ranges are ambiguous */
        for (unsigned j=0;j<20;j++) m->ranges.data[20+j]=m->ranges.data[j];
        m->ranges.bytes=40; expected=PM_MPIC_NO_RANGE;
    }
    if (which==21) { /* assigned aperture too small */
        put(m->assigned.data+16,0x60000); expected=PM_MPIC_BAD_ADDRESS;
    }
    if (which==22) { /* nonzero child origin: subtraction is necessary */
        put(m->ranges.data,0x10000); want=0x80030000;
    }
    if (which==23) { /* a BAR-relative parent token must not become an address */
        put(m->ranges.data+4,0x02008010); expected=PM_MPIC_BAD_ADDRESS;
    }
    if (which==24) { /* explicit empty same-width bridge ranges */
        copy.mpic.v[4]=copy.mpic.v[3]; copy.mpic.count=5;
        copy.mpic.v[3].ranges.present=1; copy.mpic.v[3].ranges.bytes=0;
    }
    if (which==25) { /* I/O range before memory range is unrelated */
        for (unsigned j=0;j<24;j++) pci->ranges.data[24+j]=pci->ranges.data[j];
        pci->ranges.bytes=48; put(pci->ranges.data,0x01000000);
    }
    if (which==26) { m->ac=0x40000000; expected=PM_MPIC_BAD_TREE; }
    if (which==27) { m->sc=0; expected=PM_MPIC_BAD_TREE; }
    REQUIRE(pm_mpic_resolve(&copy,&pa,&bytes,&order)==(int)expected);
    if (expected==PM_MPIC_OK) REQUIRE(pa==want && bytes==size && order==PM_MPIC_MMIO_LE);
    else REQUIRE(pa==123 && bytes==456 && order==77);
    quit(0);
}
