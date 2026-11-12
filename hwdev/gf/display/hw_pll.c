/* NV44A VPLL restore: nouveau nvkm/subdev/devinit/nv04.c:113-140,
 * 184-195, 198-273 (MIT); GF_STOP_REF_DESIGN §3-5. */
#include "hw_state.h"
#include "../internal.h"
#include "../../gfinit_mode.h"

#define PLL_HEAD_A 0x680508U
#define PLL_HEAD_B 0x680520U
#define PLL_SECOND_A 0x680578U
#define PLL_SECOND_B 0x68057cU
#define PLL_RAMDAC580 0x680580U
#define PLL_POWERCTRL_2 0x00c040U

static int pll_read(struct gf_io *io,u32 reg,u32 *value)
{
    return io->read(io->opaque,GF_MODE_REGISTER_BAR,reg,4,value);
}
static int pll_write(struct gf_io *io,u32 reg,u32 value)
{
    return io->write(io->opaque,GF_MODE_REGISTER_BAR,reg,4,value);
}

/* nv04.c:184-195: select the single-stage bit for this head. */
static u32 new_ramdac580(u32 reg1,bool single,u32 value)
{
    u32 bit=reg1==PLL_HEAD_A ? 0x00000100U : 0x10000000U;
    return single ? value|bit : value&~bit;
}

int gf4_hw_pll_restore(struct gf_io *io,u8 head,const struct gf4_hw_pllvals *pv)
{
    u32 reg1,reg2,old1,old2,pll1,pll2,old580,ramdac580,savedc040;
    u32 shift;
    bool single;
    int rc,restore_rc;

    if (!io || !io->read || !io->write || !pv || head>=GF4_HEADS)
        return GF4_EINVAL;
    reg1=head ? PLL_HEAD_B : PLL_HEAD_A;
    reg2=head ? PLL_SECOND_B : PLL_SECOND_A;

    /* nv04.c:204-208: VPLL1 and VPLL2 reads and new values. */
    rc=pll_read(io,reg1,&old1);
    if (rc) return rc;
    rc=pll_read(io,reg2,&old2);
    if (rc) return rc;
    pll1=(old1&0xfff80000U)|((u32)pv->log2p<<16)|pv->nm1;
    pll2=(old2&0x7fff0000U)|0x80000000U|pv->nm2;
    single=!pv->nm2 || (pv->nm2>>8)==(pv->nm2&0xffU);

    /* nv04.c:220-227, 262-263: RAMDAC 0x680580. */
    rc=pll_read(io,PLL_RAMDAC580,&old580);
    if (rc) return rc;
    ramdac580=new_ramdac580(reg1,single,old580);
    if (old580!=ramdac580) old1=~0U;
    if (single) pll2|=0x011fU;
    if (old1==pll1 && old2==pll2) return 0; /* nv04.c:233-234 */

    /* nv04.c:113-140, 236-241, 269-270: powerctrl_1_shift is
     * -4 for both NV44A VPLLs, so 0x001584 is never accessed. */
    /* nv04.c:243-259, 271-272: save, clear and restore 0xc040. */
    shift=head ? 18U : 16U;
    rc=pll_read(io,PLL_POWERCTRL_2,&savedc040);
    if (rc) return rc;
    rc=pll_write(io,PLL_POWERCTRL_2,savedc040&~(3U<<shift));
    if (rc) return rc;
    if (old580!=ramdac580) rc=pll_write(io,PLL_RAMDAC580,ramdac580);
    /* nv04.c:265-267: program VPLL2 before VPLL1. */
    if (!rc) rc=pll_write(io,reg2,pll2);
    if (!rc) rc=pll_write(io,reg1,pll1);
    restore_rc=pll_write(io,PLL_POWERCTRL_2,savedc040);
    return rc ? rc : restore_rc;
}
