/* GF video operations. Resource/ROM/A2 ownership is supplied by P4. */
#include "internal.h"
static int gf_failure(struct gf_display_context *c)
{
    u32 section=c->section,ordinal=c->ordinal,target=c->target;
    int rc=c->last_rc ? c->last_rc : -1;
    c->reusable=false;
    gf_display_stop(c);
    c->section=section;c->ordinal=ordinal;c->target=target;c->last_rc=rc;
    return -1;
}
static int gf_start(void *v, u32 mode_id, const u8 *rgb)
{
    struct gf_display_context *c=v;
    struct video_mode mode;
    if (!c || !rgb || gf_display_mode_info(c,0,&mode) || mode_id!=mode.id)
        return OS32_ERR_INVAL;
    if (!c->reusable) return -1;
    if (c->bars[GF_MODE_VRAM_BAR].length<GF_MODE_VRAM_BYTES) return OS32_ERR_INVAL;
    for (u32 i=0;i<VIDEO_PALETTE_BYTES;i++) c->palette[i]=rgb[i];
    c->engaged=true;c->stop_ok=false;
    if (!c->initialized) {
        /* acquire supplies borrowed init/program and limits; no retained ROM. */
        c->section=GF_DISPLAY_INIT;
        int rc=c->owner->acquire(c);
        if (!rc) rc=gf_init_run(c->init,c->program);
        if (!rc) {c->section=GF_DISPLAY_BASELINE;rc=gf_display_baseline(c);}
        c->owner->release(c);
        c->init=0;c->program=0;
        if (rc) goto fail;
        c->initialized=true;
    } else if (c->owner->validate(c)) goto fail;
    c->section=GF_DISPLAY_CALC;
    if (gfinit_mode_calc(&c->limits,&c->baseline,&c->mode) || gf_display_modeset(c)) goto fail;
    c->section=GF_DISPLAY_CHECK;
    if (gf_display_checks(c)) goto fail;
    c->active=true;c->blank=false;c->last_rc=0;
    return 0;
fail:
    return gf_failure(c);
}
static int gf_palette(void *v, u32 first, u32 count, const u8 *rgb)
{
    struct gf_display_context *c=v;
    if (!c || !rgb || first>=VIDEO_PALETTE_ENTRIES || !count || count>VIDEO_PALETTE_ENTRIES-first)
        return OS32_ERR_INVAL;
    if (!c->active) return -1;
    c->section=GF_DISPLAY_PALETTE;
    int rc=gf_display_palette(c,first,count,rgb);
    if (rc) return gf_failure(c);
    return rc;
}
static int gf_blank(void *v, int blank)
{
    struct gf_display_context *c=v;
    if (!c || (blank!=0 && blank!=1)) return OS32_ERR_INVAL;
    if (!c->active) return -1;
    c->section=GF_DISPLAY_BLANK;
    int rc=gf_display_blank(c,blank);
    if (rc) return gf_failure(c);
    return rc;
}
const struct video_ops gf_video_ops={
    gf_display_query,gf_display_mode_info,gf_start,gf_display_framebuffer,
    gf_palette,gf_display_start,gf_blank,gf_display_stop
};
/* Generic internal facade. Tables contain eight non-NULL entries (P0). */
int video_query(struct video_device *d, struct video_info *out)
{
    if (out) *out=(struct video_info){0};
    return d ? d->ops->query(d->context,out) : OS32_ERR_INVAL;
}
int video_mode_info(struct video_device *d, u32 i, struct video_mode *out)
{
    if (out) *out=(struct video_mode){0};
    return d ? d->ops->mode_info(d->context,i,out) : OS32_ERR_INVAL;
}
int video_framebuffer(struct video_device *d, struct video_fb *out)
{
    if (out) *out=(struct video_fb){0};
    return d ? d->ops->framebuffer(d->context,out) : OS32_ERR_INVAL;
}
int video_start(struct video_device *d, u32 id, const u8 *rgb)
{ return d ? d->ops->start(d->context,id,rgb) : OS32_ERR_INVAL; }
int video_palette(struct video_device *d, u32 first, u32 count, const u8 *rgb)
{ return d ? d->ops->palette(d->context,first,count,rgb) : OS32_ERR_INVAL; }
int video_display_start(struct video_device *d, u32 offset)
{ return d ? d->ops->display_start(d->context,offset) : OS32_ERR_INVAL; }
int video_blank(struct video_device *d, int blank)
{ return d ? d->ops->blank(d->context,blank) : OS32_ERR_INVAL; }
int video_stop(struct video_device *d)
{ return d ? d->ops->stop(d->context) : OS32_ERR_INVAL; }
