#include "mpic_tree.h"
/* OF PCI binding 2.1 sections 2.2.1, 4.1.2: n/space/BAR and five-cell
 * assigned-addresses. NetBSD macppc/macppc/interrupts.c:91-103 supplies the
 * macio BAR + child reg model; unlike that shortcut, walk every parent range.
 * OpenBSD macppc/dev/openpic.c:221,303: endian is a device attribute.
 * B1 identifies KeyLargo compatible; other machines need an explicit contract
 * (including big-endian property capture) before selecting a BE accessor. */
#define PCI_MEMORY 0x02000000UL
#define PCI_SPACE 0x03000000UL
#define PCI_ABSOLUTE 0x80000000UL
#define PCI_BAR0 0x10UL
#define MPIC_MIN_BYTES 0x200b4UL /* CPU0 EOI inclusive, design section 6 */
static u32 cell_at(const unsigned char *p)
{
    return (u32)p[0]<<24 | (u32)p[1]<<16 | (u32)p[2]<<8 | p[3];
}
static int number(const unsigned char *p, unsigned cells, u32 *out)
{
    if (!cells || cells>2 || (cells==2 && cell_at(p))) return 0;
    *out=cell_at(p+4*(cells-1));
    return 1;
}
static int address(const unsigned char *p, unsigned ac, u32 *hi, u32 *lo)
{
    *hi=ac==3?cell_at(p):0;
    if (ac==3) {
        if ((*hi&PCI_SPACE)!=PCI_MEMORY) return 0;
        return number(p+4,2,lo);
    }
    return number(p,ac,lo);
}
static int property(const struct pm_kboot_property *p, unsigned stride)
{
    return p->present && p->bytes<=PM_KBOOT_PROP_BYTES && stride && stride<=8 &&
           p->bytes%(stride*4)==0;
}
static int contains(u32 base, u32 size, u32 a, u32 n)
{
    return size && a>=base &&
           n<=size && a-base<=size-n;
}
/* macio's primary memory BAR, not the first assigned entry. No guessing if
 * duplicated, relocatable, non-memory, high-cell, or outside its aperture. */
static int macio_bar(const struct pm_kboot_node *v, u32 a, u32 n)
{
    unsigned found=0;
    if (!property(&v->assigned,5)) return 0;
    for (unsigned k=0;k<v->assigned.bytes;k+=20) {
        const unsigned char *p=v->assigned.data+k;
        u32 hi=cell_at(p), base, size;
        if ((hi&0xff)!=PCI_BAR0) continue;
        if (!(hi&PCI_ABSOLUTE) || !address(p,3,&hi,&base) ||
            !number(p+12,2,&size) || !contains(base,size,a,n)) return 0;
        found++;
    }
    return found==1;
}
/* PCI binding 2.1 section 11.2: resolve a parent BAR token using the
 * matching BDF/BAR assigned address before walking the PCI domain ranges. */
static int parent_address(const struct pm_kboot_node *v, u32 hi, u32 *a)
{
    unsigned found=0;
    u32 resolved=0;
    if (!property(&v->assigned,5)) return 0;
    for (unsigned k=0;k<v->assigned.bytes;k+=20) {
        const unsigned char *p=v->assigned.data+k;
        u32 ah=cell_at(p), base, size;
        if ((ah&0x03ffffffUL)!=(hi&0x03ffffffUL)) continue;
        if (!(ah&PCI_ABSOLUTE) || !address(p,3,&ah,&base) ||
            !number(p+12,2,&size) || *a>=size || *a>0xffffffffUL-base)
            return 0;
        resolved=base+*a; found++;
    }
    if (found!=1) return 0;
    *a=resolved;
    return 1;
}
int pm_mpic_resolve(const struct pm_kboot_info *info, u32 *pa, u32 *bytes, int *order)
{
    if (!info || !pa || !bytes || !order) return PM_MPIC_BAD_TREE;
    const struct pm_kboot_device *d=&info->mpic;
    if (d->count<2 || d->count>PM_KBOOT_ANCESTORS) return PM_MPIC_BAD_TREE;
    if (d->machine!=PM_KBOOT_MACHINE_KEYLARGO) return PM_MPIC_BAD_ORDER;
    const struct pm_kboot_node *bus=&d->v[1];
    u32 a, hi, n;
    if (!bus->ac || bus->ac>2 || !bus->sc || bus->sc>2)
        return PM_MPIC_BAD_TREE; /* standalone PCI PIC not in G4 scope */
    if (!property(&d->v[0].reg,bus->ac+bus->sc) ||
        d->v[0].reg.bytes!=4*(bus->ac+bus->sc)) return PM_MPIC_BAD_PROPERTY;
    if (!address(d->v[0].reg.data,bus->ac,&hi,&a) ||
        !number(d->v[0].reg.data+4*bus->ac,bus->sc,&n) ||
        n<MPIC_MIN_BYTES || n>0xffffffffUL-a) return PM_MPIC_BAD_ADDRESS;
    for (unsigned i=1;i+1<d->count;i++) {
        bus=&d->v[i];
        const struct pm_kboot_node *parent=&d->v[i+1];
        unsigned stride=bus->ac+parent->ac+bus->sc, matches=0;
        u32 next=0, next_hi=0;
        if (!bus->ac || bus->ac>3 || !parent->ac || parent->ac>3 ||
            !bus->sc || bus->sc>2 || !property(&bus->ranges,stride))
            return PM_MPIC_BAD_PROPERTY;
        if (!bus->ranges.bytes) {
            /* The decoded address fits either one or two numeric cells.
             * PCI's tagged address needs the same representation. */
            if ((bus->ac==3 || parent->ac==3) && bus->ac!=parent->ac)
                return PM_MPIC_BAD_PROPERTY;
            continue; /* explicitly empty ranges is identity; absent is not */
        }
        for (unsigned k=0;k<bus->ranges.bytes;k+=4*stride) {
            const unsigned char *p=bus->ranges.data+k;
            u32 child, ch, dest, dh, size;
            if (bus->ac==3 && (cell_at(p)&PCI_SPACE)!=PCI_MEMORY) continue;
            if (!address(p,bus->ac,&ch,&child) ||
                !address(p+4*bus->ac,parent->ac,&dh,&dest) ||
                !number(p+4*(bus->ac+parent->ac),bus->sc,&size))
                return PM_MPIC_BAD_ADDRESS;
            if ((ch&PCI_SPACE)!=(hi&PCI_SPACE) || !contains(child,size,a,n)) continue;
            if (parent->ac==3 && !(dh&PCI_ABSOLUTE) && (dh&0x00ffffffUL)) {
                if (!parent_address(bus,dh,&dest)) return PM_MPIC_BAD_ADDRESS;
                dh=PCI_MEMORY;
            }
            if (a-child>0xffffffffUL-dest || n>0xffffffffUL-(dest+a-child))
                return PM_MPIC_BAD_ADDRESS;
            next=dest+(a-child); next_hi=dh; matches++;
        }
        if (matches!=1) return PM_MPIC_NO_RANGE;
        if (parent->ac==3 && bus->ac!=3 && !macio_bar(bus,next,n))
            return PM_MPIC_BAD_ADDRESS;
        a=next; hi=next_hi;
    }
    if (hi) return PM_MPIC_BAD_ADDRESS;
    *pa=a; *bytes=n; *order=PM_MPIC_MMIO_LE;
    return PM_MPIC_OK;
}
