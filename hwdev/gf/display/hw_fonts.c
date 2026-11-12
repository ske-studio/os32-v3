/*
 * Copyright 2006 Dave Airlie
 * Copyright 2007 Maarten Maathuis
 * Copyright 2007-2009 Stuart Bennett
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN
 * AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 */
/* NV44A: nouveau dispnv04/hw.c:85-124,289-371; hw.h:194-201,222-242. */
#include "hw_state.h"
#include "../internal.h"
#include "../../../mm/pgalloc.h"

#define F_CR 0x6013d4U
#define F_SR 0x0c03c4U
#define F_GR 0x0c03ceU
#define F_AR 0x6013c0U
#define F_STATUS 0x6013daU
#define F_MISC_READ 0x0c03ccU
#define F_MISC_WRITE 0x0c03c2U
#define F_STRIDE 0x2000U
#define F_PLANE_WORDS 16384U
#define F_PLANES 4U
#define F_BYTES (F_PLANES*F_PLANE_WORDS*sizeof(u32))
#define F_PAGES (F_BYTES/PAGE_SIZE)

static int rd(struct gf_io *io,u32 port,u32 *v)
{ return io->read(io->opaque,GF_MODE_REGISTER_BAR,port,1,v); }
static int wr(struct gf_io *io,u32 port,u32 v)
{ return io->write(io->opaque,GF_MODE_REGISTER_BAR,port,1,v); }
static int idx_rd(struct gf_io *io,u32 port,u32 index,u32 *v)
{
    int rc=wr(io,port,index);
    return rc ? rc : rd(io,port+1U,v);
}
static int idx_wr(struct gf_io *io,u32 port,u32 index,u32 v)
{
    int rc=wr(io,port,index);
    return rc ? rc : wr(io,port+1U,v);
}
/* hw.c:85-104,110-124; hw.h:239-242: CR44, SR00 and SR01. */
static int blank(struct gf_io *io,u32 head,bool on)
{
    u32 seq,port=F_SR+head*F_STRIDE;
    int rc=idx_wr(io,F_CR,0x44U,head ? 3U : 0U);
    if (!rc) rc=idx_rd(io,port,1U,&seq);
    if (!rc) rc=idx_wr(io,port,0U,1U);
    if (!rc) rc=idx_wr(io,port,1U,on ? seq|0x20U : seq&~0x20U);
    if (!rc) rc=idx_wr(io,port,0U,3U);
    return rc;
}
/* hw.c:319-325; hw.h:194-201,222-236: owner, palette gate and AR10. */
static int graphics(struct gf_io *io,u32 *mode)
{
    u32 v; int rc=idx_wr(io,F_CR,0x44U,0);
    if (!rc) rc=rd(io,F_STATUS,&v);
    if (!rc) rc=wr(io,F_AR,0);
    if (!rc) rc=rd(io,F_STATUS,&v);
    if (!rc) rc=rd(io,F_AR,&v);
    if (!rc) rc=rd(io,F_STATUS,&v);
    if (!rc) rc=wr(io,F_AR,0x10U);
    if (!rc) rc=rd(io,F_AR+1U,mode);
    if (!rc) rc=rd(io,F_STATUS,&v);
    if (!rc) rc=wr(io,F_AR,0x20U);
    return rc;
}
/* hw.c:289-307,339-369: BAR1 plane window and VGA control registers. */
static int transfer(struct gf_io *io,u32 *fonts,bool save)
{
    u32 misc,seq2,seq4,gr4,gr5,gr6;
    int rc=blank(io,1,true);
    if (!rc) rc=blank(io,0,true);
    if (!rc) rc=rd(io,F_MISC_READ,&misc);
    if (!rc) rc=idx_rd(io,F_SR,2,&seq2);
    if (!rc) rc=idx_rd(io,F_SR,4,&seq4);
    if (!rc) rc=idx_rd(io,F_GR,4,&gr4);
    if (!rc) rc=idx_rd(io,F_GR,5,&gr5);
    if (!rc) rc=idx_rd(io,F_GR,6,&gr6);
    if (rc) {
        (void)blank(io,1,false);
        (void)blank(io,0,false);
        return rc;
    }
    rc=wr(io,F_MISC_WRITE,0x67U);
    if (!rc) rc=idx_wr(io,F_SR,4,6);
    if (!rc) rc=idx_wr(io,F_GR,5,0);
    if (!rc) rc=idx_wr(io,F_GR,6,5);
    for (u32 plane=0;!rc && plane<F_PLANES;plane++) {
        rc=idx_wr(io,F_SR,2,1U<<plane);
        if (!rc) rc=idx_wr(io,F_GR,4,plane);
        for (u32 i=0;!rc && i<F_PLANE_WORDS;i++) {
            u32 *p=&fonts[plane*F_PLANE_WORDS+i];
            rc=save ? io->read(io->opaque,GF_MODE_VRAM_BAR,i*4U,4,p) :
                      io->write(io->opaque,GF_MODE_VRAM_BAR,i*4U,4,*p);
        }
    }
    /* Attempt all restoration writes even after an I/O failure. */
    int cleanup=0,err;
#define RESTORE(expr) do { err=(expr); if (!cleanup && err) cleanup=err; } while (0)
    RESTORE(wr(io,F_MISC_WRITE,misc));
    RESTORE(idx_wr(io,F_GR,4,gr4));
    RESTORE(idx_wr(io,F_GR,5,gr5));
    RESTORE(idx_wr(io,F_GR,6,gr6));
    RESTORE(idx_wr(io,F_SR,2,seq2));
    RESTORE(idx_wr(io,F_SR,4,seq4));
    RESTORE(blank(io,1,false));
    RESTORE(blank(io,0,false));
#undef RESTORE
    return rc ? rc : cleanup;
}
int gf4_hw_fonts_save(struct gf_io *io,struct gf4_hw_saved *s)
{
    u32 mode,phys; int rc;
    if (!io || !s) return GF4_EINVAL;
    s->fonts=0; s->fonts_valid=false;
    rc=graphics(io,&mode);
    if (rc || (mode&1U)) return rc;
    phys=pgalloc_alloc_phys(LEDGER_OWNER_KERNEL,F_PAGES);
    if (!phys) return GF4_ENOSPC;
    s->fonts=P2V(phys);
    rc=transfer(io,s->fonts,true);
    if (rc) { gf4_hw_fonts_reset(s); return rc; }
    s->fonts_valid=true;
    return 0;
}
int gf4_hw_fonts_restore(struct gf_io *io,const struct gf4_hw_saved *s)
{
    u32 mode; int rc;
    if (!io || !s) return GF4_EINVAL;
    if (!s->fonts_valid || !s->fonts) return 0;
    rc=graphics(io,&mode);
    if (rc || (mode&1U)) return rc;
    return transfer(io,s->fonts,false);
}
void gf4_hw_fonts_reset(struct gf4_hw_saved *s)
{
    if (!s) return;
    if (s->fonts) (void)pgalloc_free_n_owner(LEDGER_OWNER_KERNEL,V2P(s->fonts)/PAGE_SIZE,F_PAGES);
    s->fonts=0; s->fonts_valid=false;
}
