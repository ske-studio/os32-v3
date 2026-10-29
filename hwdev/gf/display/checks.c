/* Required checks share their descriptors with diagnostic C. */
#include "access.h"
#include "state.h"
static int display_check(struct gf_display_context *c, u32 kind, u32 reg,
                           u32 index, u32 mask, u32 expected)
{
    u32 actual;
    if (c->io.point(c->io.opaque) ||
        gf_display_value(c,kind,reg,index,kind==GF_C_MMIO?4:1,&actual)) return -1;
    if ((actual^expected)&mask) {c->last_rc=-1;return -1;}
    return 0;
}
static u32 check_saved(const struct gf_display_context *c, u32 i)
{
    if (i==GF_DISPLAY_COUNT(c_fields)) return c->baseline_c040;
    const struct c_saved_field *f=&c_fields[i];
    u32 value=0;
    for (u32 j=0;j<f->width;j++)
        value|=(u32)((const u8 *)&c->baseline)[f->saved_offset+j]<<(8*j);
    return value;
}
int gf_display_checks(struct gf_display_context *c)
{
    const struct gfinit_mode *m=&c->mode;
#define CHECK_MMIO(reg,mask,value) do { if (display_check(c,GF_C_MMIO,reg,0,mask,value)) return -1; } while (0)
#define CHECK_INDEX(kind,index,mask,value) do { if (display_check(c,kind,0,index,mask,value)) return -1; } while (0)
#define CHECK_SAVED(i,mask,keep) do { \
    u32 field=(i); \
    if (display_check(c,field==GF_DISPLAY_COUNT(c_fields)?GF_C_MMIO:c_fields[field].kind, \
        field==GF_DISPLAY_COUNT(c_fields)?GF_PLL_GATE:c_fields[field].reg, \
        field==GF_DISPLAY_COUNT(c_fields)?0:c_fields[field].index,mask,check_saved(c,field)&(keep))) return -1; \
} while (0)
#include "checks.inc"
#undef CHECK_MMIO
#undef CHECK_INDEX
#undef CHECK_SAVED
    /* P4 owns PCI identity/BAR/Command and legacy checks; no 98 route here. */
    return c->owner->validate(c);
}
