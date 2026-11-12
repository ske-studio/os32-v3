/* NV44A restore: nouveau dispnv04/crtc.c:692-706, hw.c:827-835. */
#include "hw_state.h"
#include "../internal.h"
#include "../production.h"
#include "../../gfinit_mode.h"

#define STRIDE 0x2000U
#define CR 0x6013d4U
#define SR 0x0c03c4U
#define GR 0x0c03ceU
#define AR 0x6013c0U
#define STATUS 0x6013daU
#define DAC 0x6813c6U
#define RAMDAC 0x680000U
#define CRTC 0x600000U
#define COUNT(a) (sizeof(a)/sizeof((a)[0]))

static int rd(struct gf_io *io,u32 addr,u32 width,u32 *v)
{ return io->read(io->opaque,GF_MODE_REGISTER_BAR,addr,width,v); }
static int wr(struct gf_io *io,u32 addr,u32 width,u32 v)
{ return io->write(io->opaque,GF_MODE_REGISTER_BAR,addr,width,v); }
/* dispnv04/hw.h:119-134: indexed VGA registers. */
static int idx_wr(struct gf_io *io,u32 port,u32 index,u32 v)
{
    int rc=wr(io,port,1,index);
    return rc ? rc : wr(io,port+1U,1,v);
}
static int idx_rd(struct gf_io *io,u32 port,u32 index,u32 *v)
{
    int rc=wr(io,port,1,index);
    return rc ? rc : rd(io,port+1U,1,v);
}
/* hw.h:194-199: status resets the attribute controller flip-flop. */
static int gate(struct gf_io *io,u8 h,bool enable)
{
    u32 v,off=h*STRIDE; int rc=rd(io,STATUS+off,1,&v);
    return rc ? rc : wr(io,AR+off,1,enable ? 0U : 0x20U);
}
/* hw.h:243-255: SR01, SR00, and the attribute gate. */
static int protect(struct gf_io *io,u8 h,bool on)
{
    u32 port=SR+h*STRIDE,v; int rc=idx_rd(io,port,1,&v);
    if (!rc && on) rc=idx_wr(io,port,0,1);
    if (!rc) rc=idx_wr(io,port,1,on ? v|0x20U : v&~0x20U);
    if (!rc && !on) rc=idx_wr(io,port,0,3);
    return rc ? rc : gate(io,h,on);
}
struct reg_field { u32 reg,offset; };
#define FIELD(reg,field) {reg,__builtin_offsetof(struct gf4_hw_crtc_reg,field)}
/* hw.c:487-501,518-535; nvreg.h: RAMDAC register addresses. */
static const struct reg_field ramdac_regs[]={
 FIELD(0x600,ramdac_gen_ctrl), FIELD(0x630,ramdac_630), FIELD(0x634,ramdac_634),
 FIELD(0x700,tv_setup), FIELD(0x720,tv_vtotal), FIELD(0x724,tv_vskew),
 FIELD(0x728,tv_vsync_delay), FIELD(0x72c,tv_htotal), FIELD(0x730,tv_hskew),
 FIELD(0x734,tv_hsync_delay), FIELD(0x738,tv_hsync_delay2)
};
static const struct reg_field ramdac_tail[]={
 FIELD(0x848,fp_control), FIELD(0x880,fp_debug_0), FIELD(0x884,fp_debug_1),
 FIELD(0x888,fp_debug_2), FIELD(0x84c,fp_margin_color), FIELD(0x8c0,ramdac_8c0),
 FIELD(0xa20,ramdac_a20), FIELD(0xa24,ramdac_a24), FIELD(0xa34,ramdac_a34)
};
static int fields(struct gf_io *io,u32 base,const struct gf4_hw_crtc_reg *p,
                  const struct reg_field *f,u32 count)
{
    for (u32 i=0;i<count;i++) {
        int rc=wr(io,base+f[i].reg,4,*(const u32 *)((const u8 *)p+f[i].offset));
        if (rc) return rc;
    }
    return 0;
}
static int load_ramdac(struct gf_io *io,u8 h,const struct gf4_hw_saved *s)
{
    const struct gf4_hw_crtc_reg *p=&s->crtc[h]; u32 base=RAMDAC+h*STRIDE;
    /* hw.c:477-483: CURSYNC, VPLL, PLL_COEFF_SELECT, SEL_CLK. */
    int rc=wr(io,base+0x404U,4,p->nv10_cursync);
    if (!rc) rc=gf4_hw_pll_restore(io,h,&p->pllvals);
    if (!rc) rc=wr(io,RAMDAC+0x50cU,4,s->pllsel);
    if (!rc) rc=wr(io,RAMDAC+0x524U,4,s->sel_clk);
    if (!rc) rc=fields(io,base,p,ramdac_regs,COUNT(ramdac_regs));
    /* hw.c:503-515: FP timing and dither. */
    for (u32 i=0;!rc && i<7;i++) {
        rc=wr(io,base+0x800U+4U*i,4,p->fp_vert_regs[i]);
        if (!rc) rc=wr(io,base+0x820U+4U*i,4,p->fp_horiz_regs[i]);
    }
    if (!rc) rc=wr(io,base+0x83cU,4,p->dither);
    for (u32 i=0;!rc && i<3;i++) {
        rc=wr(io,base+0x850U+4U*i,4,p->dither_regs[i]);
        if (!rc) rc=wr(io,base+0x85cU+4U*i,4,p->dither_regs[i+3]);
    }
    if (!rc) rc=fields(io,base,p,ramdac_tail,COUNT(ramdac_tail));
    for (u32 i=0;!rc && i<38;i++) rc=wr(io,base+0xc00U+4U*i,4,p->ctv_regs[i]);
    return rc;
}
/* hw.c:712-742,759-769; nvreg.h: CIO indices. */
static const u8 ext_first[]={0x19,0x1a,0x25,0x28,0x33,0x2d,0x1c,0x1b,0x20,0x47,0x9f,0x49,0x30,0x31,0x2f};
static const u8 ext_middle[]={0x39,0x3b,0x3c,0x41,0x45,0x4b,0x52,0x42,0x53,0x54};
static const u8 ext_last[]={0x59,0x5b,0x85,0x86};
static int cr_list(struct gf_io *io,u32 port,const struct gf4_hw_crtc_reg *p,const u8 *a,u32 n)
{
    for (u32 i=0;i<n;i++) { int rc=idx_wr(io,port,a[i],p->cr[a[i]]); if (rc) return rc; }
    return 0;
}
static int load_ext(struct gf_io *io,u8 h,const struct gf4_hw_crtc_reg *p)
{
    u32 port=CR+h*STRIDE,base=CRTC+h*STRIDE,ram=RAMDAC+h*STRIDE,v,amount;
    /* hw.c:674-690: ENGINE_CTRL precedes CR33; stop PVIDEO, set limits. */
    int rc=wr(io,base+0x860U,4,p->crtc_eng_ctrl);
    if (!rc) rc=wr(io,0x8704U,4,1);
    if (!rc) rc=wr(io,0x8140U,4,0);
    if (!rc) rc=wr(io,0x8920U,4,0);
    if (!rc) rc=wr(io,0x8924U,4,0);
    if (!rc) rc=rd(io,GF_PRODUCTION_PFB_AMOUNT,4,&amount);
    if (!rc) amount=(amount&GF_PRODUCTION_PFB_MASK)-1U;
    if (!rc) rc=wr(io,0x8908U,4,amount);
    if (!rc) rc=wr(io,0x890cU,4,amount);
    if (!rc) rc=wr(io,0x8808U,4,amount);
    if (!rc) rc=wr(io,0x880cU,4,amount);
    if (!rc) rc=wr(io,0x1588U,4,0);
    /* hw.c:692-710: cursor, GPIO, 900 bit16 and CRTC CONFIG. */
    if (!rc) rc=wr(io,base+0x810U,4,p->cursor_cfg);
    if (!rc) rc=wr(io,base+0x830U,4,p->crtc_830);
    if (!rc) rc=wr(io,base+0x834U,4,p->crtc_834);
    if (!rc) rc=wr(io,base+0x81cU,4,p->gpio_ext);
    if (!rc) rc=wr(io,base+0x850U,4,p->crtc_850);
    if (!rc) rc=rd(io,ram+0x900U,4,&v);
    if (!rc) rc=wr(io,ram+0x900U,4,p->crtc_cfg==2U ? v|0x10000U : v&~0x10000U);
    if (!rc) rc=wr(io,base+0x804U,4,p->crtc_cfg);
    if (!rc) rc=cr_list(io,port,p,ext_first,COUNT(ext_first));
    /* hw.c:732-733; hw.h:341-349: rewrite cursor position. */
    if (!rc) rc=rd(io,ram+0x300U,4,&v);
    if (!rc) rc=wr(io,ram+0x300U,4,v);
    if (!rc) rc=cr_list(io,port,p,ext_middle,COUNT(ext_middle));
    /* hw.c:763-766; hw.h:151-156: CR57 indexes CR58. */
    for (u32 i=0;!rc && i<16;i++) {
        rc=idx_wr(io,port,0x57U,i);
        if (!rc) rc=idx_wr(io,port,0x58U,p->cr58[i]);
    }
    if (!rc) rc=cr_list(io,port,p,ext_last,COUNT(ext_last));
    /* hw.c:772: PCRTC_START. PCRTC_INTR_EN belongs to F1 (design §9). */
    if (!rc) rc=wr(io,base+0x800U,4,p->fb_start);
    return rc;
}
/* hw.c:795-811: pixel mask, palette index, 768 values, gate. */
static int load_palette(struct gf_io *io,u8 h,const struct gf4_hw_crtc_reg *p)
{
    u32 off=h*STRIDE; int rc=wr(io,DAC+off,1,0xffU);
    if (!rc) rc=wr(io,DAC+off+2U,1,0);
    for (u32 i=0;!rc && i<768;i++) rc=wr(io,DAC+off+3U,1,p->dac[i]);
    return rc ? rc : gate(io,h,false);
}
/* hw.c:564-586; hw.h:208-219,272-284: VGA and CR11 lock. */
static int load_vga(struct gf_io *io,u8 h,const struct gf4_hw_crtc_reg *p)
{
    u32 off=h*STRIDE,v,port=CR+off; int rc=wr(io,0x0c03c2U+off,1,p->misc);
    for (u32 i=0;!rc && i<5;i++) rc=idx_wr(io,SR+off,i,p->sr[i]);
    if (!rc) rc=idx_rd(io,port,0x11U,&v);
    if (!rc) rc=idx_wr(io,port,0x11U,v&~0x80U);
    for (u32 i=0;!rc && i<25;i++) rc=idx_wr(io,port,i,p->cr[i]);
    if (!rc) rc=idx_rd(io,port,0x11U,&v);
    if (!rc) rc=idx_wr(io,port,0x11U,v|0x80U);
    for (u32 i=0;!rc && i<9;i++) rc=idx_wr(io,GR+off,i,p->gr[i]);
    if (!rc) rc=gate(io,h,true);
    for (u32 i=0;!rc && i<21;i++) {
        u32 index;
        rc=rd(io,STATUS+off,1,&v);
        if (!rc) rc=rd(io,AR+off,1,&v);
        if (!rc) index=i|(v&0x20U);
        if (!rc) rc=rd(io,STATUS+off,1,&v);
        if (!rc) rc=wr(io,AR+off,1,index);
        if (!rc) rc=wr(io,AR+off,1,p->ar[i]);
    }
    return rc ? rc : gate(io,h,false);
}
int gf4_hw_crtc_restore(struct gf_io *io,u8 h,const struct gf4_hw_saved *s)
{
    if (!io || !s || h>=GF4_HEADS) return GF4_EINVAL;
    /* crtc.c:699-703; hw.h:286-305: owner, load, saved CR21. */
    int rc=idx_wr(io,CR,0x44U,h ? 3U : 0U);
    if (!rc) rc=protect(io,h,true);
    if (!rc) rc=load_ramdac(io,h,s);
    if (!rc) rc=load_ext(io,h,&s->crtc[h]);
    if (!rc) rc=load_palette(io,h,&s->crtc[h]);
    if (!rc) rc=load_vga(io,h,&s->crtc[h]);
    if (!rc) rc=protect(io,h,false);
    if (!rc) rc=idx_wr(io,CR+h*STRIDE,0x21U,s->crtc[h].cr[0x21]);
    return rc;
}
