/* NV44A save path: nouveau dispnv04/crtc.c:680-684, hw.c:131-202,
 * 393-465, 540-560, 590-661, 776-791, 814-824; hw.h:158-163,194-236. */
#include "hw_state.h"
#include "../internal.h"
#define BAR GF_MODE_REGISTER_BAR
#define STRIDE 0x2000U
#define CR 0x6013d4U
#define SR 0x0c03c4U
#define GR 0x0c03ceU
#define AR 0x6013c0U
#define STATUS 0x6013daU
#define DAC 0x6813c6U
#define RAMDAC 0x680000U
#define CRTC 0x600000U

static int rd(struct gf_io *io,u32 addr,u32 width,u32 *v)
{ return io->read(io->opaque,BAR,addr,width,v); }
static int wr(struct gf_io *io,u32 addr,u32 width,u32 v)
{ return io->write(io->opaque,BAR,addr,width,v); }
static int idx_rd(struct gf_io *io,u32 port,u32 index,u8 *out)
{
    u32 v; int rc=wr(io,port,1,index);
    if (!rc) rc=rd(io,port+1U,1,&v);
    if (!rc) *out=(u8)v;
    return rc;
}
static int idx_wr(struct gf_io *io,u32 port,u32 index,u32 v)
{
    int rc=wr(io,port,1,index);
    return rc ? rc : wr(io,port+1U,1,v);
}
/* dispnv04/hw.h:194-201: reset flip-flop then set palette gate. */
static int gate(struct gf_io *io,u8 h,bool enable)
{
    u32 v,off=h*STRIDE; int rc=rd(io,STATUS+off,1,&v);
    return rc ? rc : wr(io,AR+off,1,enable ? 0U : 0x20U);
}
static int save_pll(struct gf_io *io,u8 h,u32 ref,struct gf4_hw_pllvals *p)
{
    u32 a,b; int rc=rd(io,h ? 0x680520U : 0x680508U,4,&a);
    if (!rc) rc=rd(io,h ? 0x68057cU : 0x680578U,4,&b);
    if (rc) return rc;
    /* hw.c:139-151: NV44A uses the two-register VPLL branch. */
    p->log2p=(a>>16)&7U; p->nm1=a&0xffffU;
    p->nm2=(b&0x80000000U) ? (b&0xffffU) : 0x0101U;
    p->refclk=ref; return 0;
}
struct reg_field { u32 reg,offset; };
#define FIELD(reg,field) {reg,__builtin_offsetof(struct gf4_hw_crtc_reg,field)}
/* Addresses and order: hw.c:400-424,433,440-461; nvreg.h:330-444. */
static const struct reg_field ramdac_regs[]={
 FIELD(0x404,nv10_cursync), FIELD(0x600,ramdac_gen_ctrl),
 FIELD(0x630,ramdac_630), FIELD(0x634,ramdac_634),
 FIELD(0x700,tv_setup), FIELD(0x720,tv_vtotal), FIELD(0x724,tv_vskew),
 FIELD(0x728,tv_vsync_delay), FIELD(0x72c,tv_htotal), FIELD(0x730,tv_hskew),
 FIELD(0x734,tv_hsync_delay), FIELD(0x738,tv_hsync_delay2)
};
static const struct reg_field ramdac_tail[]={
 FIELD(0x848,fp_control), FIELD(0x880,fp_debug_0), FIELD(0x884,fp_debug_1),
 FIELD(0x888,fp_debug_2), FIELD(0x84c,fp_margin_color), FIELD(0x8c0,ramdac_8c0),
 FIELD(0xa20,ramdac_a20), FIELD(0xa24,ramdac_a24), FIELD(0xa34,ramdac_a34)
};
static int fields(struct gf_io *io,u32 base,struct gf4_hw_crtc_reg *p,
                  const struct reg_field *f,u32 count)
{
    for (u32 i=0;i<count;i++) {
        int rc=rd(io,base+f[i].reg,4,(u32 *)((u8 *)p+f[i].offset));
        if (rc) return rc;
    }
    return 0;
}
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))
static int save_ramdac(struct gf_io *io,u8 h,u32 ref,struct gf4_hw_saved *s)
{
    struct gf4_hw_crtc_reg *p=&s->crtc[h]; u32 base=RAMDAC+h*STRIDE;
    int rc=rd(io,base+0x404U,4,&p->nv10_cursync);
    if (!rc) rc=save_pll(io,h,ref,&p->pllvals);
    if (!rc) rc=rd(io,RAMDAC+0x50cU,4,&s->pllsel);
    if (!rc) rc=rd(io,RAMDAC+0x524U,4,&s->sel_clk);
    if (!rc) rc=fields(io,base,p,ramdac_regs+1,COUNT(ramdac_regs)-1);
    for (u32 i=0;!rc && i<7;i++) {
        rc=rd(io,base+0x800U+4U*i,4,&p->fp_vert_regs[i]);
        if (!rc) rc=rd(io,base+0x820U+4U*i,4,&p->fp_horiz_regs[i]);
    }
    if (!rc) rc=rd(io,base+0x83cU,4,&p->dither);
    for (u32 i=0;!rc && i<3;i++) {
        rc=rd(io,base+0x850U+4U*i,4,&p->dither_regs[i]);
        if (!rc) rc=rd(io,base+0x85cU+4U*i,4,&p->dither_regs[i+3]);
    }
    if (!rc) rc=fields(io,base,p,ramdac_tail,COUNT(ramdac_tail));
    for (u32 i=0;!rc && i<38;i++) rc=rd(io,base+0xc00U+4U*i,4,&p->ctv_regs[i]);
    return rc;
}
/* hw.c:546-560; hw.h:204-236: attribute reads retain current gate. */
static int save_vga(struct gf_io *io,u8 h,struct gf4_hw_crtc_reg *p)
{
    u32 off=h*STRIDE,v; int rc=rd(io,0x0c03ccU+off,1,&v);
    if (!rc) p->misc=(u8)v;
    for (u32 i=0;!rc && i<25;i++) rc=idx_rd(io,CR+off,i,&p->cr[i]);
    if (!rc) rc=gate(io,h,true);
    for (u32 i=0;!rc && i<21;i++) {
        rc=rd(io,STATUS+off,1,&v);
        if (!rc) rc=rd(io,AR+off,1,&v);
        if (!rc) rc=rd(io,STATUS+off,1,&v);
        if (!rc) rc=wr(io,AR+off,1,i);
        if (!rc) rc=rd(io,AR+off+1U,1,&v);
        if (!rc) p->ar[i]=(u8)v;
    }
    if (!rc) rc=gate(io,h,false);
    for (u32 i=0;!rc && i<9;i++) rc=idx_rd(io,GR+off,i,&p->gr[i]);
    for (u32 i=0;!rc && i<5;i++) rc=idx_rd(io,SR+off,i,&p->sr[i]);
    return rc;
}
static int save_palette(struct gf_io *io,u8 h,struct gf4_hw_crtc_reg *p)
{
    u32 off=h*STRIDE,v; int rc=wr(io,DAC+off,1,0xffU);
    if (!rc) rc=wr(io,DAC+off+1U,1,0);
    for (u32 i=0;!rc && i<768;i++) {
        rc=rd(io,DAC+off+3U,1,&v); if (!rc) p->dac[i]=(u8)v;
    }
    return rc ? rc : gate(io,h,false);
}
/* hw.c:597-661: CR reads occur on both sides of the PCRTC reads. */
static const u8 ext_first[]={0x33,0x19,0x1a,0x25,0x28,0x2d,0x1c,0x1b,0x20,0x21,0x47,0x9f,0x49,0x30,0x31,0x2f,0x39};
static const u8 ext_middle[]={0x3b,0x3c,0x41,0x45,0x4b,0x52,0x42,0x53,0x54};
static const u8 ext_last[]={0x59,0x5b,0x85,0x86};
static const struct reg_field crtc_regs[]={
 FIELD(0x830,crtc_830), FIELD(0x834,crtc_834), FIELD(0x81c,gpio_ext),
 FIELD(0x850,crtc_850), FIELD(0x860,crtc_eng_ctrl), FIELD(0x810,cursor_cfg),
 FIELD(0x804,crtc_cfg)
};
static int cr_list(struct gf_io *io,u32 port,struct gf4_hw_crtc_reg *p,const u8 *a,u32 n)
{
    for (u32 i=0;i<n;i++) { int rc=idx_rd(io,port,a[i],&p->cr[a[i]]); if (rc) return rc; }
    return 0;
}
static int save_ext(struct gf_io *io,u8 h,struct gf4_hw_crtc_reg *p)
{
    u32 port=CR+h*STRIDE,base=CRTC+h*STRIDE;
    int rc=cr_list(io,port,p,ext_first,COUNT(ext_first));
    if (!rc) rc=fields(io,base,p,crtc_regs,COUNT(crtc_regs));
    if (!rc) rc=cr_list(io,port,p,ext_middle,COUNT(ext_middle));
    for (u32 i=0;!rc && i<16;i++) {
        rc=idx_wr(io,port,0x57U,i);
        if (!rc) rc=idx_rd(io,port,0x58U,&p->cr58[i]);
    }
    if (!rc) rc=cr_list(io,port,p,ext_last,COUNT(ext_last));
    if (!rc) rc=rd(io,base+0x800U,4,&p->fb_start);
    return rc;
}
int gf4_hw_crtc_save(struct gf_io *io,u8 h,u32 ref,struct gf4_hw_saved *s)
{
    /* crtc.c:680-684; hw.c:821-824: owner then RAMDAC, VGA, palette, ext. */
    int rc=idx_wr(io,CR,0x44U,h ? 3U : 0U);
    if (!rc) rc=save_ramdac(io,h,ref,s);
    if (!rc) rc=save_vga(io,h,&s->crtc[h]);
    if (!rc) rc=save_palette(io,h,&s->crtc[h]);
    if (!rc) rc=save_ext(io,h,&s->crtc[h]);
    return rc;
}
