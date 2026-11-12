/* Encoder state: nouveau c03c1a1 dispnv04, MIT. */
#include "hw_state.h"
#include "../foundation.h"

/* dac.c:41-52,475-495; tvnv17.c:613-635. */
#define ENC_STRIDE 0x2000U
#define ENC_DACCLK 0x68052cU
/* dfp.c:55-66; nvreg.h FP_TMDS_CONTROL/DATA. */
#define ENC_TMDS_CONTROL 0x6808b0U
#define ENC_TMDS_DATA 0x6808b4U
#define ENC_TMDS_READ ((1U<<16)|4U)
/* tvnv17.h:137-155, tvmodesnv17.c:355-434. */
#define ENC_TV_INDEX 0xd220U
#define ENC_TV_DATA 0xd224U
#define ENC_PTV 0xd000U
#define ENC_HFILTER 0xd310U
#define ENC_HFILTER2 0xd390U
#define ENC_VFILTER 0xd510U
static const u32 enc_ptv_offsets[]={
    0x200U,0x204U,0x208U,0x20cU,0x304U,0x500U,0x504U,
    0x508U,0x600U,0x604U,0x608U,0x60cU,0x610U,0x614U
};
static const u32 enc_filter_offsets[]={0,0x1cU,0x40U,0x5cU};

static i32 enc_read(struct gf_io *io,u32 off,u32 *v)
{ return io->read(io->opaque,GF_MODE_REGISTER_BAR,off,4,v); }
static i32 enc_write(struct gf_io *io,u32 off,u32 v)
{ return io->write(io->opaque,GF_MODE_REGISTER_BAR,off,4,v); }
static u32 enc_output(const struct gf4_route *r)
{ return ((r->or_mask&12U) ? 0x68U : 0)+((r->or_mask&10U) ? ENC_STRIDE : 0); }

/* tvnv17.h:150-155: index then data, both 32-bit PTV accesses. */
static i32 enc_tv_read(struct gf_io *io,u32 index,u8 *v)
{
    u32 value;
    i32 rc=enc_write(io,ENC_TV_INDEX,index);
    if (!rc) rc=enc_read(io,ENC_TV_DATA,&value);
    if (!rc) *v=(u8)value;
    return rc;
}
static i32 enc_tv_write(struct gf_io *io,u32 index,u8 v)
{
    i32 rc=enc_write(io,ENC_TV_INDEX,index);
    return rc ? rc : enc_write(io,ENC_TV_DATA,v);
}
/* tvmodesnv17.c:355-405: encoder, three filters, then PTV. */
static i32 enc_tv_save(struct gf_io *io,struct gf4_tv_state *tv)
{
    i32 rc=0;
    for (u32 i=0;!rc && i<64U;++i) rc=enc_tv_read(io,i,&tv->enc[i]);
    const u32 bases[]={ENC_HFILTER,ENC_HFILTER2,ENC_VFILTER};
    u32 (*filters[])[7]={tv->hfilter,tv->hfilter2,tv->vfilter};
    for (u32 f=0;!rc && f<3U;++f)
        for (u32 i=0;!rc && i<4U;++i)
            for (u32 j=0;!rc && j<7U;++j)
                rc=enc_read(io,bases[f]+enc_filter_offsets[i]+4U*j,&filters[f][i][j]);
    for (u32 i=0;!rc && i<14U;++i)
        rc=enc_read(io,ENC_PTV+enc_ptv_offsets[i],&tv->ptv[i]);
    return rc;
}
/* tvmodesnv17.c:367-378,406-434: same order, then latch 3e. */
static i32 enc_tv_load(struct gf_io *io,const struct gf4_tv_state *tv)
{
    i32 rc=0;
    for (u32 i=0;!rc && i<64U;++i) rc=enc_tv_write(io,i,tv->enc[i]);
    const u32 bases[]={ENC_HFILTER,ENC_HFILTER2,ENC_VFILTER};
    const u32 (*filters[])[7]={tv->hfilter,tv->hfilter2,tv->vfilter};
    for (u32 f=0;!rc && f<3U;++f)
        for (u32 i=0;!rc && i<4U;++i)
            for (u32 j=0;!rc && j<7U;++j)
                rc=enc_write(io,bases[f]+enc_filter_offsets[i]+4U*j,filters[f][i][j]);
    for (u32 i=0;!rc && i<14U;++i)
        rc=enc_write(io,ENC_PTV+enc_ptv_offsets[i],tv->ptv[i]);
    if (!rc) rc=enc_tv_write(io,0x3eU,1);
    if (!rc) rc=enc_tv_write(io,0x3eU,0);
    return rc;
}
/* hw.c:205-212: PLL byte pairs are N:M, clock in kHz. */
static u32 enc_clock(const struct gf4_hw_pllvals *p)
{
    u32 m1=p->nm1&0xffU,n1=p->nm1>>8,m2=p->nm2&0xffU,n2=p->nm2>>8;
    if (!m1 || !m2) return 0;
    return (n1*n2*p->refclk/(m1*m2))>>p->log2p;
}
int gf4_hw_enc_save(struct gf_io *io,const struct gf4_route *routes,u32 n,
                    struct gf4_hw_saved *saved)
{
    if (!io || !io->read || !io->write || !saved || (n && !routes) || n>32U)
        return GF4_EINVAL;
    saved->nroutes=n;
    for (u32 i=0;i<n;++i) {
        const struct gf4_route *r=&routes[i];
        struct gf4_hw_output *out=&saved->output[i];
        i32 rc=0;
        if (r->type==GF4_ANALOG || r->type==GF4_TV) {
            rc=enc_read(io,ENC_DACCLK+enc_output(r),&out->output);
            if (!rc && r->type==GF4_TV) rc=enc_tv_save(io,&out->tv);
        } else if (r->type==GF4_TMDS || r->type==GF4_LVDS) {
            u32 ramdac=(r->or_mask&4U)>>2,value;
            rc=enc_write(io,ENC_TMDS_CONTROL+ramdac*ENC_STRIDE,ENC_TMDS_READ);
            if (!rc) rc=enc_read(io,ENC_TMDS_DATA+ramdac*ENC_STRIDE,&value);
            if (!rc) out->head=(i32)(((value>>3)&1U)^ramdac);
        }
        if (rc) return rc;
    }
    return 0;
}
int gf4_hw_enc_restore(struct gf4_ctx *c,struct gf_io *io,const struct gf4_route *routes,
                       u32 n,const struct gf4_hw_saved *saved,bool clock_ok,u32 dl)
{
    if (!io || !io->write || !saved || (n && !routes) || n!=saved->nroutes || n>32U)
        return GF4_EINVAL;
    i32 first=0;
    for (u32 i=0;i<n;++i) {
        const struct gf4_route *r=&routes[i];
        const struct gf4_hw_output *out=&saved->output[i];
        i32 rc=0;
        if (r->type==GF4_ANALOG || r->type==GF4_TV) {
            rc=enc_write(io,ENC_DACCLK+enc_output(r),out->output);
            if (r->type==GF4_TV) {
                i32 tv_rc=enc_tv_load(io,&out->tv);
                if (!rc) rc=tv_rc;
            }
        } else if (r->type==GF4_TMDS && clock_ok) {
            if (!c || !c->exec || !c->exec->tmds_script || out->head<0 || out->head>=GF4_HEADS)
                rc=GF4_ESTATE;
            else {
                struct gf4_route bound=*r;
                bound.head=(u8)out->head;
                rc=c->exec->tmds_script(c->exec->owner,&bound,
                                        enc_clock(&saved->crtc[out->head].pllvals),dl);
            }
        } else if (r->type==GF4_LVDS) rc=GF4_EUNSUPPORTED;
        if (!first && rc) first=rc;
    }
    return first;
}
