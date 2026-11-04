/* UNIT 2: real PLL calculations. Oracle: nouveau dispnv04/crtc.c:91-102,
 * 153-162,688 and nvreg.h:314-323; nv17 TV has no PLLSEL bits
 * (tvnv17.c:403-577). Link the production PLL solver and sequence. */
#define GF4_HOST_UNIT 2
#include "gf4_display_fake.h"

static struct gf4_head_image heads[GF4_HEADS];
void _start(void)
{
    u32 value = 0;
    assert(gf4_pllsel_calc(heads, &value) == GF4_OK && value == 0x00000004U);
    heads[0].enabled = true;
    heads[0].route.or_mask = 1;
    assert(gf4_pllsel_calc(heads, &value) == GF4_OK && value == 0x10000204U);
    heads[1].enabled = true;
    heads[1].route.head = 1;
    heads[1].route.or_mask = 2;
    heads[1].route.type = GF4_TV;
    heads[1].dpms = GF4_OFF;
    assert(gf4_pllsel_calc(heads, &value) == GF4_OK && value == 0x30000a04U);
    heads[0].enabled = false;
    assert(gf4_pllsel_calc(heads, &value) == GF4_OK && value == 0x20000804U);
    heads[0].enabled = true;
    heads[1].route.or_mask = 1;
    value = 0xdeadbeefU;
    assert(gf4_pllsel_calc(heads, &value) == GF4_EBUSY && value == 0xdeadbeefU);
    heads[1].route.or_mask = 2;
    heads[1].route.type = 255;
    assert(gf4_pllsel_calc(heads, &value) == GF4_EUNSUPPORTED && value == 0xdeadbeefU);
    assert(gf4_pllsel_calc(0, &value) == GF4_EINVAL && value == 0xdeadbeefU);
    assert(gf4_pllsel_calc(heads, 0) == GF4_EINVAL);

    /* Solver fixture uses the known exact 148500 kHz case in
     * tools/tests/gfinit_pll_host.c:62; 0.5% is design section 3. */
    struct gfinit_pll_limits limits = {0};
    limits.ref = 27000;
    limits.vco1 = (struct gfinit_pll_vco){100000,500000,27000,27000,1,255,1,1};
    struct gf4_mode mode = {0}; mode.t.khz = 148500;
    struct gfinit_mode_saved saved = {0};
    struct gfinit_mode out = {0};
    struct gf4_route route = {0};
    assert(gf4_pll_calc(&mode, &route, &saved, &limits, &out) == GF4_OK);
    assert(out.pll.actual == 148500 && out.pllsel == 0x10000204U);
    assert(out.sequence.count == 5);
    route.head = 1;
    assert(gf4_pll_calc(&mode, &route, &saved, &limits, &out) == GF4_OK);
    assert(out.pllsel == 0x20000804U && out.sequence.count == 5);
    assert(out.sequence.writes[3].reg == 0x680520U); /* nv04.c:184-193 */
    mode.t.khz = 0;
    out.pllsel = 0xdeadbeefU;
    assert(gf4_pll_calc(&mode, &route, &saved, &limits, &out) == GF4_EPLL);
    assert(out.pllsel == 0xdeadbeefU);
    gf2_host_exit(0);
}
