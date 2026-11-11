#include "drivers/video/geforce/gf_v2.h"
#include "hwdev/gf/display/api.h"
#include "kprintf.h"
struct gf_v2 gf_v2_inst;
static struct gf4_state state;
static struct gf4_route route;
static struct gf4_ctx c;
static struct gf4_test_ops ops;
static struct gf2_chan ch;
static struct gf_foundation foundation;
static u32 ticks, pal_calls, mode_id, saved_off, saved_id, saved_gen, fail_mode;
static u8 palette[768];
const struct gf4_test_ops *gf_v2_disp(void) { return &ops; }
int gf_v2_deadline(struct gf2_chan *ch0, u32 us, u32 *dl) { (void)ch0; *dl=us; return 0; }
int gf_v2_start(struct gf_v2 *g, u32 dl) { (void)dl; g->lost=0; return 0; }
int gf_v2_rc(int rc) { return rc; }
u32 gf_v2_route_index(const struct gf4_ctx *c0, const struct gf4_route *r) { (void)c0; (void)r; return 0; }
void __cdecl kprintf(u8 attr, const char *fmt, ...) { (void)attr; (void)fmt; }
void gf_irq_snapshot(struct gf_foundation *f, struct gf_irq_stats *s) { (void)f; *s=(struct gf_irq_stats){0}; s->vblank[0]=ticks; }
i32 gf4_palette_set(struct gf4_ctx *c0,u8 h,u32 first,u32 count,const u8 *rgb) { (void)c0; (void)h; pal_calls++; for (u32 i=0;i<count*3;i++) palette[first*3+i]=rgb[i]; return 0; }
i32 gf4_flip_complete(struct gf4_ctx *c0,u8 h,u32 dl) { (void)dl; c0->hold[h].pending=0; return 0; }
static int set_mode(struct gf4_ctx *c0,const struct gf4_route *r,u32 id,const struct gf3_surface *s,u32 dl) { (void)r; (void)dl; mode_id=id; saved_off=s->off; saved_id=s->reservation_id; saved_gen=s->generation; if (fail_mode) return -9; c0->state->head[0].front=*s; return 0; }
static int flip(struct gf4_ctx *c0,u8 h,const struct gf3_surface *s,bool sync,u32 dl) { (void)s; (void)sync; (void)dl; c0->hold[h].pending=1; c0->hold[h].base_verified=1; c0->hold[h].after=ticks; return 0; }
#define CHECK(x,n) do { if (!(x)) return n; } while (0)
static int test(void)
{
    struct gf_v2 *g=&gf_v2_inst; struct gfx2_surf s={0}, alias={0}; GFX2_ModeInfo m={0}; u32 retired, colors[2]={0x123456,0xabcdef};
    ops.ctx=&c; ops.set_mode=set_mode; ops.flip=flip; c.ch=&ch; ch.f=&foundation; c.state=&state; c.routes=&route; c.nroutes=1; route.head_mask=1;
    state.head[0].enabled=1; state.head[0].front=(struct gf3_surface){.off=3,.fmt=GF3_INDEX8,.reservation_id=8,.generation=9}; state.head[0].mode.id=17; state.head[0].palette[30]=91;
    s.sid=7; s.fmt=GFX2_FMT_RGB565; s.gpu_off=10; s.priv[0]=11; s.priv[1]=12; m.id=19;
    CHECK(!gf_v2_mode_set(g,0,1,&m,&s) && g->home.valid && g->home.mode_id==17 && g->home.front.off==3 && g->home.pal[30]==91 && saved_off==10 && saved_id==11 && saved_gen==12,1);
    alias.flags=GFX2_SF_ALIAS; CHECK(!gf_v2_mode_set(g,0,1,&m,&alias) && !g->home.valid && mode_id==17 && saved_off==3 && palette[30]==91,2);
    fail_mode=1; CHECK(gf_v2_mode_set(g,0,1,&m,&s)==-9 && !g->home.valid,3); fail_mode=0;
    state.head[0].front.fmt=GF3_RGB565; pal_calls=0;
    CHECK(!gf_v2_palette(g,10,2,colors) && !pal_calls && g->pal[30]==0x12 && g->pal[35]==0xef,4);
    s.fmt=GFX2_FMT_INDEX8; CHECK(!gf_v2_mode_set(g,0,1,&m,&s) && pal_calls==1 && palette[30]==0x12 && palette[35]==0xef,5);
    s.sid=8; CHECK(!gf_v2_flip(g,0,&s,0,1) && g->head[0].pend_sid==8,6);
    CHECK(gf_v2_flip_done(g,0,&retired)==OS32_ERR_BUSY,7); ticks++;
    CHECK(!gf_v2_flip_done(g,0,&retired) && retired==7 && !gf_v2_flip_done(g,0,&retired) && !retired,8);
    g->node[0].value.owner=g; g->node[0].value.reservation_id=11; g->node[0].value.generation=12;
    CHECK(gf_v2_owns(&(struct gf3_surface){.reservation_id=11,.generation=12}) && !gf_v2_owns(&(struct gf3_surface){.reservation_id=11,.generation=13}),9);
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80" : : "a"(1),"b"(rc)); __builtin_unreachable(); }
