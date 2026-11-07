/* The bounded byte protocol is shared with the existing BSD-derived ESCC. */
#include "ofw.h"
#include "ppc_frame.h"
#include "ppc_memmap.h"
static struct pm_escc console;
void pm_mmu_console_bind(u32 pa)
{
    console.csr=pm_p2v_io(pa);
    console.data=console.csr+PM_ESCC_STRIDE;
}
static void text(const char *p)
{
    unsigned n=0;
    while (p[n]) n++;
    if (pm_escc_write(&console,p,n)) ppc_stop();
}
static void hex(u32 x)
{
    char b[9];
    for (unsigned i=0; i<8; i++) b[i]="0123456789abcdef"[(x>>(28-4*i))&15];
    b[8]=0; text(b);
}
void pm_mmu_dump(const char *tag, const struct ppc_frame *f)
{
    text(tag);
    if (f) {
        text(" VECTOR="); hex(f->vector);
        text(" SRR0="); hex(f->srr0); text(" SRR1="); hex(f->srr1);
        text(" DAR="); hex(f->dar); text(" DSISR="); hex(f->dsisr);
        for (unsigned i=0; i<32; i++) { text(" R"); hex(i); text("="); hex(f->gpr[i]); }
        text(" LR="); hex(f->lr); text(" CR="); hex(f->cr);
        text(" CTR="); hex(f->ctr); text(" XER="); hex(f->xer);
    }
    text("\r\n");
    if (pm_escc_drain(&console)) ppc_stop();
}
