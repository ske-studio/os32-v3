#include "gfx/v2/gfx2_sw.h"
#include "kstring.h"
static struct gfx2_adapter a;
static struct gfx2_surf surfaces[3];
u32 gfx2_pal[256];
static u8 pixels[3][256], cpu[512];
static u32 colors[64], done, fence;
static int hw, user = 1, mode, calls, emits, fail_at, emit_rc, commit_rc, kicks;
static int lost, abandon, begins, ends, bad, sw_id, rb_write, readback_calls, check_calls;
static int deny_read, invisible, wait_rc;
static u32 deny_write, observed_color, seen_fence, events[32], nevents;
static struct gfx2_rect observed_clip;
static u8 *observed_base;
static GFX2_Op ops[4];
static GfxSyncCtx *sync_ctx = (GfxSyncCtx *)0x1234;
void *kmemset(void *p, int c, u32 n) { u8 *d=p; while(n--) *d++=c; return p; }
void *kmemcpy(void *p, const void *q, u32 n) { u8 *d=p; const u8 *s=q; while(n--) *d++=*s++; return p; }
int ring3_call_from_user(void) { return user; }
int ring3_user_range_ok(u32 p, u32 n) { return p && n && !deny_read; }
int ring3_user_range_writable(u32 p, u32 n) { return p && n && p != deny_write; }
struct gfx2_adapter *gfx2_adapter_get(u32 i) { return i ? 0 : &a; }
int gfx2_caller_get(struct gfx2_caller *c) { c->owner=7; c->as=0; return 0; }
struct gfx2_surf *gfx2_surf_get(u32 sid, u32 owner, int *rc)
{
    *rc=OS32_ERR_INVAL;
    if (owner != 7 || sid < 1 || sid > 3) return 0;
    *rc=0; return &surfaces[sid-1];
}
int gfx2_cpu_visible(const struct gfx2_surf *s) { (void)s; return !invisible; }
int gfx2_sync_get(const struct gfx2_adapter *p, u32 h, GfxSyncCtx **c, u32 *m)
{ (void)p; (void)h; *c=sync_ctx; *m=mode; return 0; }
int gfx2_fmt_info(u32 f, struct gfx2_fmt_info *v)
{ kmemset(v,0,sizeof(*v)); if (!f || f>10) return OS32_ERR_INVAL; v->cpp=f==1||f==7||f==8?1:f==3||f==4?4:2; return 0; }
static void observe(const struct gfx2_exec *e)
{
    const GFX2_Op *o=e->op;
    observed_clip=e->clip; observed_base=e->dv.base;
    if (rb_write==1 && o->op==GFX2_OP_FILL && o->fg.v==42) bad=1;
    if (e->dst && (e->dv.pal != gfx2_pal || e->dv.pitch != surfaces[o->dst-1].pitch)) bad=1;
    if (e->dst && e->dv.sync != ((e->dst->flags & GFX2_SF_ALIAS) && mode==GFXSYNC_DIRECT ? sync_ctx : 0)) bad=1;
    if (e->pat && e->pat->kind==2) observed_color=e->pat->color[0];
    if (o->op==GFX2_OP_HOSTDATA && *e->cpu != 0x6d) bad=1;
}
static int backend_check(void *c, const struct gfx2_exec *e)
{
    (void)c; check_calls++;
    /* Odd fg uses HW, even fg SW in mixed batches. */
    return hw && (hw==1 || (e->op->fg.v & 1)) ? 0 : OS32_ERR_NOSYS;
}
static int emit(void *c, const struct gfx2_exec *e, u32 f)
{
    (void)c; emits++; calls++; events[nevents++]=1; seen_fence=f; observe(e);
    return fail_at==emits ? emit_rc : 0;
}
static int readback(void *c, const struct gfx2_exec *e, u8 *out)
{
    (void)c; readback_calls++; calls++; observe(e);
    if (rb_write==1) { GFX2_Op replacement={0}; replacement.op=GFX2_OP_FILL; replacement.dst=1; replacement.dw=replacement.dh=2; replacement.rop=GFX2_ROP_PATCOPY; replacement.fg.v=42; kmemcpy(out,&replacement,sizeof(replacement)); }
    else if (rb_write==2) *out=0x6d;
    else kmemset(out,0x91,e->op->sw);
    return 0;
}
static int kick(void *c) { (void)c; kicks++; events[nevents++]=3; return 0; }
static const GfxBackendV2 be={.check=backend_check,.emit=emit,.readback=readback,.kick=kick};
u32 gfx2_fence_reserve(struct gfx2_adapter *p) { return p->fence.reserved=p->fence.issued+1; }
int gfx2_fence_commit(struct gfx2_adapter *p,u32 f,struct gfx2_surf *d,struct gfx2_surf *s)
{ (void)s; events[nevents++]=2; p->fence.reserved=0; if(commit_rc) { lost++; return commit_rc; } p->fence.issued=f; if(d)d->last_write=f; return 0; }
void gfx2_fence_commit_fail(struct gfx2_adapter *p,u32 f,struct gfx2_surf *d,struct gfx2_surf *s,int rc)
{ (void)d;(void)s;(void)rc; p->fence.reserved=0; p->fence.failed_upto=f; lost++; }
void gfx2_fence_abandon(struct gfx2_adapter *p,u32 f) { (void)f; p->fence.reserved=0; abandon++; }
void gfx2_adapter_lost(struct gfx2_adapter *p,int held) { (void)p;(void)held; lost++; }
int gfx2_cpu_begin(struct gfx2_surf *s,u32 access,u32 dl)
{ (void)s;(void)access; if(dl!=1000000)bad=1; begins++; events[nevents++]=4; return wait_rc; }
void gfx2_cpu_end(struct gfx2_surf *s,u32 access,const struct gfx2_rect *r)
{ (void)s;(void)access;(void)r; ends++; }
void gfx2_closing_sweep(struct gfx2_adapter *p) { (void)p; }
static int sw(const struct gfx2_exec *e,struct gfx2_rect *r,int id)
{ calls++; sw_id=id; observe(e); *r=e->clip; return 0; }
#define SW(name,id) int name(const struct gfx2_exec *e,struct gfx2_rect *r) { return sw(e,r,id); }
SW(gfx2_sw_fill,1) SW(gfx2_sw_copy,2) SW(gfx2_sw_stretch,3) SW(gfx2_sw_mono,4)
SW(gfx2_sw_hostdata,5) SW(gfx2_sw_rop4,7) SW(gfx2_sw_alpha,8) SW(gfx2_sw_line,9)
SW(gfx2_sw_rop,11) SW(gfx2_sw_ckey,12)
int gfx2_sw_readback(const struct gfx2_exec *e,u8 *out) { return readback(0,e,out); }
static GFX2_Op shape(u32 op)
{
    GFX2_Op o={0}; o.op=op;
    if(op!=6 && op!=10) {o.dst=1;o.dw=o.dh=2;}
    if(op==2||op==3||op==6||op==7||op==8) {o.src=2;o.sw=o.sh=2;}
    if(op==1||op==9)o.rop=GFX2_ROP_PATCOPY;
    if(op==2||op==3)o.rop=GFX2_ROP_SRCCOPY;
    if(op==4||op==5||op==6||op==7||op==10) {o.data=(u32)(uptr)cpu; o.pitch=8;}
    if(op==9) {o.dw=o.dh=0;o.sx=o.sy=4;}
    if(op==10) o.pitch=0;
    return o;
}
static void reset(void)
{
    kmemset(&a,0,sizeof(a)); kmemset(surfaces,0,sizeof(surfaces));
    a.flags=GFX2_AF_ACTIVE; a.gen=5; a.be=&be; a.fence.issued=20; a.caps.vram_cpu_bytes=1;
    for(int i=0;i<3;i++) { surfaces[i].sid=i+1; surfaces[i].w=surfaces[i].h=8;
        surfaces[i].pitch=32; surfaces[i].fmt=GFX2_FMT_XRGB8888; surfaces[i].state=GFX2_ST_IDLE;
        surfaces[i].pool=GFX2_POOL_SYSMEM; surfaces[i].kva=pixels[i]; }
    hw=mode=calls=emits=fail_at=emit_rc=commit_rc=kicks=lost=abandon=begins=ends=bad=0;
    sw_id=rb_write=readback_calls=check_calls=deny_read=invisible=wait_rc=0;
    deny_write=observed_color=seen_fence=nevents=0; done=fence=99;
    ops[0]=shape(1);ops[1]=shape(1);ops[2]=shape(1);ops[3]=shape(1);
}
#define REQUIRE(c,n) do { if(!(c)) return n; } while(0)
#define SUB(n) gfx2_submit(0,ops,n,5,&done,&fence)
static int test(void)
{
    reset(); REQUIRE(!SUB(0)&&done==0&&fence==20&&!calls,1);
    deny_write=(u32)(uptr)&fence; REQUIRE(SUB(1)==OS32_ERR_INVAL&&!calls&&done==0,2);
    reset(); REQUIRE(!SUB(3),3);
    reset(); ops[2].src=2; REQUIRE(SUB(3)==OS32_ERR_INVAL&&done==0&&!calls,3);
    reset(); hw=1; fail_at=3;emit_rc=OS32_ERR_IO;
    REQUIRE(SUB(3)==OS32_ERR_IO&&done==2&&fence==22&&lost==1&&emits==3&&kicks==1,4);
    REQUIRE(events[0]==1&&events[1]==2&&events[2]==1&&events[3]==2&&seen_fence==23,5);
    reset(); ops[0]=shape(6);ops[0].data=(u32)(uptr)&ops[1];ops[0].pitch=sizeof(GFX2_Op);rb_write=1;
    REQUIRE(!SUB(2)&&done==2&&ops[1].fg.v==42&&calls==2&&sw_id==1&&!bad,6);
    reset(); ops[0].flags=GFX2_F_PAT; REQUIRE(SUB(1)==OS32_ERR_INVAL&&!calls,7);
    for(u32 fmt=1;fmt<=3;fmt++) {
        reset(); for(int j=0;j<64;j++)colors[j]=0x12345678;
        ops[0]=shape(10);ops[0].flags=GFX2_F_PAT_COLOR;ops[0].data=(u32)(uptr)colors;
        ops[1].flags=GFX2_F_PAT;surfaces[0].fmt=fmt;
        REQUIRE(!SUB(2)&&done==2&&observed_color==(fmt==1?0x78u:fmt==2?0x5678u:0x12345678u)&&sw_id==11,8);
    }
    reset(); ops[0]=shape(6);ops[0].data=(u32)(uptr)&done;ops[0].pitch=8;
    REQUIRE(!SUB(1)&&done==1&&fence==20,9);
    reset(); ops[0]=shape(6);ops[0].data=(u32)(uptr)cpu;rb_write=2;ops[1]=shape(5);
    REQUIRE(!SUB(2)&&!bad&&readback_calls==1&&sw_id==5,10);
    reset(); hw=2; ops[0].fg.v=1;ops[1].fg.v=2;ops[2].fg.v=3;
    REQUIRE(!SUB(3)&&done==3&&fence==22&&emits==2&&begins==1&&ends==1&&kicks==1,11);
    REQUIRE(events[0]==1&&events[1]==2&&events[2]==4&&events[3]==1&&events[4]==2&&events[5]==3,12);
    reset(); hw=1;fail_at=1;emit_rc=OS32_ERR_NOSYS;
    REQUIRE(SUB(1)==OS32_ERR_NOSYS&&done==0&&fence==20&&abandon==1&&!lost&&!kicks,13);
    reset(); hw=1;commit_rc=OS32_ERR_IO;
    REQUIRE(SUB(1)==OS32_ERR_IO&&done==0&&fence==20&&lost==1,14);
    reset(); invisible=1; REQUIRE(SUB(1)==OS32_ERR_NOSYS&&!calls&&done==0,15);
    reset(); ops[0]=shape(5);ops[0].pitch=0xffffffff;
    REQUIRE(SUB(1)==OS32_ERR_INVAL&&!calls,16);
    reset(); ops[0]=shape(6);ops[0].sx=7; REQUIRE(SUB(1)==OS32_ERR_INVAL&&!calls,17);
    reset(); surfaces[0].state=GFX2_ST_LOCKED_RO; REQUIRE(SUB(1)==OS32_ERR_BUSY&&!calls,18);
    reset(); ops[0]=shape(2);surfaces[1].state=GFX2_ST_LOCKED_RO; REQUIRE(!SUB(1),19);
    surfaces[1].state=GFX2_ST_LOCKED_RW; REQUIRE(SUB(1)==OS32_ERR_BUSY,20);
    reset(); ops[0]=shape(3);ops[0].src=1; REQUIRE(SUB(1)==OS32_ERR_INVAL,21);
    reset(); ops[0]=shape(2);ops[0].rop=0x66; REQUIRE(!SUB(1)&&sw_id==11,22);
    ops[0].rop=GFX2_ROP_SRCCOPY;ops[0].flags=GFX2_F_SRCKEY; REQUIRE(!SUB(1)&&sw_id==12,23);
    reset(); surfaces[0].flags=GFX2_SF_ALIAS;mode=GFXSYNC_DIRECT;
    REQUIRE(!SUB(1)&&!bad,24); mode=GFXSYNC_SHADOW; REQUIRE(!SUB(1)&&!bad,25);
    reset(); wait_rc=OS32_ERR_TIMEOUT;REQUIRE(SUB(1)==OS32_ERR_TIMEOUT&&!calls&&done==0,26);
    /* Actual C8 and public shape query agree for every valid op/format/pool. */
    for(u32 op=1;op<=10;op++) for(u32 fmt=1;fmt<=10;fmt++) for(u32 pool=1;pool<=3;pool++) for(int engine=0;engine<2;engine++) {
        u32 how; reset();hw=engine;ops[0]=shape(op);
        surfaces[0].fmt=surfaces[1].fmt=fmt;surfaces[0].pool=surfaces[1].pool=pool;
        if(op==5||op==6) ops[0].pitch=8;
        REQUIRE(!gfx2_check_op(0,&ops[0],fmt,fmt,pool,pool,&how),27);
        int rc=SUB(1);
        if(op==10) REQUIRE(!rc&&done==1,28);
        else if(how==GFX2_HOW_NOSUPPORT) REQUIRE(rc==OS32_ERR_NOSYS&&!calls,29);
        else REQUIRE(!rc&&!bad&&((how==GFX2_HOW_HW)?(emits+readback_calls==1):(emits==0)),30);
    }
    reset();ops[0].flags=GFX2_F_CLIP;ops[0].dx=-1;ops[0].dy=-1;ops[0].cx=0;ops[0].cy=0;ops[0].cw=ops[0].ch=1;
    REQUIRE(!SUB(1),31);
    reset();deny_read=1;REQUIRE(SUB(1)==OS32_ERR_INVAL&&done==99,32);
    reset();a.flags=0;REQUIRE(SUB(1)==OS32_ERR_NOSYS&&!calls,33);
    reset();REQUIRE(gfx2_submit(0,ops,1,4,&done,&fence)==OS32_ERR_STALE&&!calls,34);
    reset();REQUIRE(SUB(65)==OS32_ERR_INVAL&&!calls,35);
    reset();hw=1;ops[0]=shape(6);REQUIRE(!SUB(1)&&readback_calls==1&&!emits&&!bad,36);
    reset(); ops[0]=shape(10);ops[0].flags=GFX2_F_PAT_COLOR;ops[0].data=(u32)(uptr)colors;
    ops[1].flags=GFX2_F_PAT;ops[2]=shape(10);ops[2].flags=GFX2_F_PAT_COLOR;
    ops[2].data=(u32)(uptr)cpu;kmemset(cpu,0x21,256);ops[3].flags=GFX2_F_PAT;
    REQUIRE(!SUB(4)&&done==4&&observed_color==0x21212121,37);
    reset();ops[0].flags=GFX2_F_PAT;REQUIRE(SUB(1)==OS32_ERR_INVAL,38);
    reset();ops[0].flags=GFX2_F_CLIP;ops[0].dx=-1;ops[0].dy=-1;ops[0].cw=ops[0].ch=1;
    REQUIRE(!SUB(1)&&observed_clip.x==0&&observed_clip.y==0&&observed_clip.w==1&&observed_clip.h==1,39);
    reset();surfaces[0].kva=0;surfaces[0].map_va=(u32)(uptr)pixels[0];
    REQUIRE(!SUB(1)&&observed_base==pixels[0],40);
    reset();ops[0].rop=GFX2_ROP_SRCCOPY;REQUIRE(SUB(1)==OS32_ERR_INVAL,41);
    reset();ops[0]=shape(8);ops[0].fg.x=256;REQUIRE(SUB(1)==OS32_ERR_INVAL,42);
    reset();ops[0]=shape(6);ops[0].flags=GFX2_F_CLIP;REQUIRE(SUB(1)==OS32_ERR_INVAL,43);
    reset();ops[0]=shape(4);ops[0].bg.x=2;REQUIRE(SUB(1)==OS32_ERR_INVAL,44);
    reset();ops[0]=shape(5);ops[0].data=0xfffffffc;REQUIRE(SUB(1)==OS32_ERR_INVAL,45);
    reset();ops[0]=shape(5);ops[0].pitch=7;REQUIRE(SUB(1)==OS32_ERR_INVAL,46);
    reset();ops[0]=shape(2);ops[0].sw=1;REQUIRE(SUB(1)==OS32_ERR_INVAL,47);
    reset();surfaces[0].adapter=1;REQUIRE(SUB(1)==OS32_ERR_INVAL,48);
    reset();surfaces[0].state=GFX2_ST_LOST;REQUIRE(SUB(1)==OS32_ERR_STALE,49);
    reset();ops[0]=shape(4);deny_write=(u32)(uptr)cpu;REQUIRE(!SUB(1),50);
    reset();ops[0]=shape(6);deny_write=(u32)(uptr)cpu;REQUIRE(SUB(1)==OS32_ERR_INVAL&&!calls,51);
    /* Cross-format and cross-pool queries use the same real participating surfaces. */
    for(u32 sf=1;sf<=10;sf++) for(u32 df=1;df<=10;df++) for(u32 sp=1;sp<=3;sp++) for(u32 dp=1;dp<=3;dp++) {
        u32 how;reset();ops[0]=shape(3);surfaces[0].fmt=df;surfaces[1].fmt=sf;
        surfaces[0].pool=dp;surfaces[1].pool=sp;
        REQUIRE(!gfx2_check_op(0,&ops[0],sf,df,sp,dp,&how),52);
        int rc=SUB(1);REQUIRE(how==GFX2_HOW_NOSUPPORT ? rc==OS32_ERR_NOSYS : rc==0,53);
    }
    return 0;
}
void _start(void) { int rc=test(); __asm__ volatile("int $0x80"::"a"(1),"b"(rc)); for(;;){} }
