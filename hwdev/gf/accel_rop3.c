/* HAL v2 G7: pattern and ROP3 channel state, HAL_V2_DESIGN section 11-6. */
#include "accel.h"

#define M_OBJECT 0x0000U
#define M_FORMAT 0x0300U
#define M_SELECT 0x030cU
#define M_MONO 0x0310U
#define M_COLOR8 0x0400U
#define M_COLOR16 0x0500U
#define M_OPERATION 0x02fcU

int gf3_rop3_set(struct gf3_ctx *c, u8 subc, u8 rop3,
                 const struct gf3_pattern *pat, enum gf3_fmt fmt, u32 dl)
{
    struct gf3_format f;
    u32 words[32], value, kind = pat ? pat->kind : 0;
    int rc;

    if (!c || !c->ch || kind > GF3_PAT_COLOR) return GF3_EINVAL;
    rc = gf3_format_get(fmt, &f);
    if (rc) return rc;
    if (f.cpp == 4) return GF3_EUNSUPPORTED;
    if (fmt != GF3_INDEX8 && fmt != GF3_RGB565) return GF3_EFORMAT;

    value = c->obj[GF3_OBJ_PATTERN].handle;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, M_OBJECT, false, &value, 1, dl);
    if (rc) goto failed;
    value = f.pat;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, M_FORMAT, false, &value, 1, dl);
    if (rc) goto failed;
    value = kind == GF3_PAT_COLOR ? 2 : 1;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, M_SELECT, false, &value, 1, dl);
    if (rc) goto failed;
    if (kind == GF3_PAT_COLOR) {
        u32 per = f.cpp == 1 ? 4 : 2;
        u32 mask = f.cpp == 1 ? 0xffU : 0xffffU;
        u32 count = 64 / per;
        for (u32 i = 0; i < count; i++) {
            words[i] = 0;
            for (u32 j = 0; j < per; j++)
                words[i] |= (pat->color[i * per + j] & mask) << (j * f.cpp * 8);
        }
        rc = gf2_emit(c->ch, GF3_SUBC_MISC, f.cpp == 1 ? M_COLOR8 : M_COLOR16,
                      false, words, count, dl);
    } else {
        if (kind == GF3_PAT_MONO) {
            u32 hi = f.cpp == 1 ? 0xffffff00U : 0xffff0000U;
            words[0] = pat->bg | hi; words[1] = pat->fg | hi;
            words[2] = pat->mono[0]; words[3] = pat->mono[1];
        } else {
            words[0] = words[1] = words[2] = words[3] = ~0U;
        }
        rc = gf2_emit(c->ch, GF3_SUBC_MISC, M_MONO, false, words, 4, dl);
    }
    if (rc) goto failed;
    value = c->obj[GF3_OBJ_ROP].handle;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, M_OBJECT, false, &value, 1, dl);
    if (rc) goto failed;
    value = rop3;
    rc = gf2_emit(c->ch, GF3_SUBC_MISC, M_FORMAT, false, &value, 1, dl);
    if (rc) goto failed;
    value = 1;
    rc = gf2_emit(c->ch, subc, M_OPERATION, false, &value, 1, dl);
failed:
    if (rc) c->last_rc = rc;
    return rc;
}
