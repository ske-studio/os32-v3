/* BAR0 aliases only. Bus adapter retains index/lock/window cleanup state. */
#ifndef GF_DISPLAY_ACCESS_H
#define GF_DISPLAY_ACCESS_H
#include "../internal.h"
static inline int gf_display_read(struct gf_display_context *c, u32 off, u32 width, u32 *v)
{
    c->target=off;
    c->last_rc=c->io.read(c->io.opaque,GF_MODE_REGISTER_BAR,off,width,v);
    return c->last_rc ? -1 : 0;
}
static inline int gf_display_write(struct gf_display_context *c, u32 off, u32 width, u32 v)
{
    c->target=off;
    c->last_rc=c->io.write(c->io.opaque,GF_MODE_REGISTER_BAR,off,width,v);
    return c->last_rc ? -1 : 0;
}
static inline u32 gf_display_alias(u32 kind)
{
    return kind==GF_C_CR ? RECOVER_PRMCIO+RECOVER_CRTC :
        RECOVER_PRMVIO+(kind==GF_C_SR ? RECOVER_SEQ : RECOVER_GFX);
}
static inline int gf_display_value(struct gf_display_context *c, u32 kind, u32 reg,
                                    u32 index, u32 width, u32 *v)
{
    if (kind==GF_C_CR || kind==GF_C_SR || kind==GF_C_GR) {
        reg=gf_display_alias(kind);
        if (gf_display_write(c,reg,1,index)) return -1;
        reg++;
    } else if (kind==GF_C_MISC) reg=RECOVER_PRMVIO+RECOVER_MISC_READ;
    return gf_display_read(c,reg,width,v);
}
#endif
