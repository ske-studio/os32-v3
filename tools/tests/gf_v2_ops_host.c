/* G1: real adapter, real ROP3/GX state setters and real G4 fence ring. */
#include "../../drivers/video/geforce/gf_v2_ops.c"
#include "drivers/video/geforce/geforce_module.h"
#include "hwdev/gf/display/api.h"
static struct gf_foundation f;
static struct gf2_chan ch = {.f = &f, .state = GF2_RUN, .epoch = 1};
static struct gf3_ctx g3 = {.ch = &ch, .ready = true, .epoch = 1};
static struct gf4_ctx display = {.ch = &ch, .accel = &g3, .ready = true, .accepting = true};
static const struct gf4_test_ops display_ops = {.ctx = &display};
static const struct gf4_test_ops *display_slot = &display_ops;
static const GeforceModuleContext context = {.display_out = &display_slot};
const GeforceModuleContext *geforce_context = &context;
int gf2_fault_check(struct gf2_chan *c) { (void)c; return 0; }
static u32 count, select_kind, bits[2], draw_at, mark_at, last_dl, clock_calls;
static int draw_rc, io_rc, restore_fail, gart_calls;
static struct gf3_rect last_rect;
static struct gf3_surface last_surface;
static struct gf_vram_reservation_node backing;
static int alias_test;
static int valid_surface(const struct gf3_surface *s)
{
    if (!alias_test) return 0;
    struct gf_vram_reservation v;
    return gf_vram_match(&ch, 0, s->reservation_id, s->generation, s->off, s->bytes, &v);
}
static void die(int n) { if (n) { char msg[]="FAIL 0000\n"; int v=n; for (int i=8;i>=5;i--) { msg[i]='0'+v%10;v/=10; } __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(msg),"d"(sizeof(msg)-1)); } __asm__ volatile("int $0x80" : : "a"(1), "b"(n ? 1 : 0)); __builtin_unreachable(); }
#define CHECK(x) do { if (!(x)) die(__LINE__); } while (0)
static int clock_us(void *o, u32 *now) { (void)o; clock_calls++; *now = 100; return io_rc; }
int gf3_format_get(enum gf3_fmt fmt, struct gf3_format *out)
{ *out = (struct gf3_format){.cpp = fmt == GF3_INDEX8 ? 1 : 2, .pat = fmt == GF3_INDEX8 ? 3 : 1}; return 0; }
int gf2_emit(struct gf2_chan *c,u8 subc,u16 m,bool ni,const u32 *v,u16 n,u32 dl)
{
    (void)c;(void)subc;(void)ni; count++; last_dl = dl;
    if (m == 0x30c) select_kind = v[0];
    if (m == 0x310 && n == 4) { bits[0] = v[2]; bits[1] = v[3]; }
    if (restore_fail && draw_at) return GF2_EIO;
    return 0;
}
void gf3_range_query_set(struct gf3_ctx *c, gf3_range_fn q, void *o)
{ c->range_q=q; c->range_opaque=o; }
int gf_v2_gart_start(struct gf_v2 *g,u32 dl)
{ CHECK(g->g3->range_q == gf_v2_gart_range && g->g3->range_opaque == g && dl == 100 + GF3_TIMEOUT_US); gart_calls++; return 0; }
void gf_v2_gart_reset(struct gf_v2 *g) { CHECK(g->ch->state == GF2_OFF); gart_calls++; }
int gf3_fill_emit(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,u32 color,u8 alu,u32 mask,u32 dl)
{
    int rc = valid_surface(s); if (rc) return rc;
    (void)color; last_surface=*s; last_rect=r; last_dl=dl;
    if (alu != GF3_ALU_KEEP) { int rc=gf3_rop_set(c, GF3_SUBC_GDI, alu, mask, s->fmt, dl); if (rc) return rc; }
    draw_at=++count; return draw_rc;
}
int gf3_copy_emit(struct gf3_ctx *c,const struct gf3_surface *s,const struct gf3_surface *d,struct gf3_rect r,u32 x,u32 y,u8 alu,u32 mask,u32 dl)
{ if (valid_surface(s)) return GF3_ESTATE; CHECK((alias_test || s->dma == GF3_HANDLE_GART_DMA) && x==2 && y==3 && r.x==6 && r.y==8); return gf3_fill_emit(c,d,r,0,alu,mask,dl); }
int gf3_scale_emit(struct gf3_ctx *c,const struct gf3_surface *s,const struct gf3_surface *d,struct gf3_rect sr,struct gf3_rect dr,struct gf3_rect cl,bool b,u32 dl)
{ (void)c;(void)sr;(void)dr;(void)cl;(void)b;(void)dl; return valid_surface(s) ? GF3_ESTATE : valid_surface(d); }
int gf3_mono(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,const struct gf3_ram *b,u32 fg,u32 bg,struct gf3_rect clip,u32 dl)
{ (void)c; if (valid_surface(s)) return GF3_ESTATE; (void)r;(void)b;(void)fg;(void)bg;(void)clip; last_dl=dl; draw_at=++count; return draw_rc; }
int gf3_upload(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,const struct gf3_ram *ram,bool force,u32 dl)
{ (void)c; if (valid_surface(s)) return GF3_ESTATE; (void)r;(void)ram; CHECK(!force); last_dl=dl; draw_at=++count; return draw_rc; }
int gf3_download(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,struct gf3_ram *ram,u32 dl)
{ (void)c;(void)s; CHECK(r.x==4 && r.y==5 && ram->pitch==16); last_dl=dl; return draw_rc; }
int gf3_fini(struct gf3_ctx *c,u32 dl) { (void)dl; c->ready=false; return draw_rc; }
int gf2_kick(struct gf2_chan *c) { (void)c; return io_rc; }
int gf2_fence_emit(struct gf2_chan *c,struct gf2_fence *mark,u32 dl)
{ last_dl=dl; mark_at=++count; *mark=(struct gf2_fence){c->epoch,++c->seq}; return io_rc; }
int gf2_fence_wait(struct gf2_chan *c,struct gf2_fence mark,u32 dl)
{ (void)c;(void)mark; last_dl=dl; return io_rc; }
int gf2_fence_poll(struct gf2_chan *c,struct gf2_fence mark,int *done)
{ (void)c;(void)mark; *done=1; return io_rc; }
void _start(void)
{
    struct gfx2_surf d={.w=32,.h=32,.pitch=64,.bytes=2048,.fmt=GFX2_FMT_RGB565,.pool=GFX2_POOL_VRAM,.priv={7,9}};
    struct gfx2_surf s=d; s.pool=GFX2_POOL_GART;
    GFX2_Op op={.op=GFX2_OP_FILL,.rop=0xf0,.dx=1,.dy=1,.dw=8,.dh=8,.fg={.v=0x1234}};
    struct gfx2_pattern p={.kind=2,.mono={0x80,0,0,0,0,0,0,1}};
    struct gfx2_exec ex={.op=&op,.dst=&d,.src=&s,.pat=&p,.clip={2,3,4,5}};
    void *g=gf_backend_v2.ctx;
    f.io.timing.clock_us=clock_us;
    u32 start_dl;
    CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    CHECK(!gf_v2_deadline(&ch, GF3_TIMEOUT_US, &start_dl));
    CHECK(!gf_v2_start(g,start_dl) && gart_calls==1);
    u32 base,size,phys; ch.B=16384; f.imem.vram_bytes=16384; ch.bar1.base=0x10000000;
    CHECK(!gf_vram_registry_init(&ch));
    CHECK(!gf_vram_reserve(&ch, &backing, 0, 4096, &backing));
    CHECK(!gf_backend_v2.heap_range(g,&base,&size,&phys));
    CHECK(base==4096 && size==12288 && phys==0x10000000);
    CHECK(!gf_backend_v2.check(g,&ex));
    CHECK(!gf_backend_v2.emit(g,&ex,1));
    CHECK(draw_at && !mark_at && last_rect.x==2 && last_rect.w==4);
    CHECK(last_surface.reservation_id==7 && last_surface.generation==9);
    CHECK(!gf_backend_v2.fence_emit(g,1,7) && mark_at>draw_at && last_dl==107);
    CHECK(!gf_backend_v2.fence_wait(g,1,9) && last_dl==109);
    op.flags=GFX2_F_PAT; op.rop=0x5a;
    for (int failure=0; failure<2; failure++) {
        count=draw_at=0; draw_rc=failure ? GF3_EINVAL : 0;
        CHECK(gf_backend_v2.emit(g,&ex,2)==(failure ? OS32_ERR_INVAL : 0));
        CHECK(select_kind==1 && bits[0]==~0u && bits[1]==~0u && count>draw_at);
        /* v1 GXxor sees MONO/all-one, since GX never resets SELECT. */
        CHECK(!gf3_rop_set(&g3,GF3_SUBC_GDI,6,~0u,GF3_RGB565,42) && select_kind==1);
    }
    p.kind=1; struct gf3_pattern packed=pattern(&ex);
    CHECK(packed.mono[0]==0x80 && packed.mono[1]==0x01000000);
    draw_rc=0; draw_at=0; restore_fail=1;
    CHECK(gf_backend_v2.emit(g,&ex,2)==OS32_ERR_IO); restore_fail=0;
    op.flags=0; op.op=GFX2_OP_COPY; op.rop=0xcc; op.sx=5; op.sy=6;
    CHECK(!gf_backend_v2.emit(g,&ex,3));
    op.flags=GFX2_F_SRCKEY; CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    op.flags=GFX2_F_DSTKEY; CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    op.flags=0; op.op=GFX2_OP_ALPHA; CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    op.op=GFX2_OP_READBACK; CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    s.pool=GFX2_POOL_VRAM; op.pitch=16; op.sx=4; op.sy=5;
    CHECK(!gf_backend_v2.check(g,&ex) && !gf_backend_v2.readback(g,&ex,0));
    op.op=GFX2_OP_HOSTDATA; d.pool=GFX2_POOL_GART; CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    d.pool=GFX2_POOL_VRAM; op.dx=2; op.dy=3; op.pitch=ex.cpu_pitch=16;
    CHECK(!gf_backend_v2.emit(g,&ex,4));
    op.op=GFX2_OP_MONO; CHECK(!gf_backend_v2.emit(g,&ex,5));
    draw_rc=GF2_ETIME; CHECK(gf_backend_v2.emit(g,&ex,6)==OS32_ERR_TIMEOUT); draw_rc=0;
    /* Real I3 order: a borrowed DISPLAY's first HW FILL, then every HW path.
     * The fake engine uses the real registry identity/range validator. */
    alias_test = 1;
    d.flags = s.flags = GFX2_SF_ALIAS;
    d.priv[0] = d.priv[1] = s.priv[0] = s.priv[1] = 0;
    s.pool = GFX2_POOL_VRAM;
    op.sx=5; op.sy=6; op.dx=1; op.dy=1;
    const u32 ops[] = {GFX2_OP_FILL, GFX2_OP_COPY, GFX2_OP_STRETCH,
                       GFX2_OP_MONO, GFX2_OP_HOSTDATA};
    for (u32 i=0; i<sizeof(ops)/sizeof(ops[0]); i++) {
        op.op=ops[i]; op.rop=op.op==GFX2_OP_FILL ? 0xf0 : 0xcc;
        CHECK(!gf_backend_v2.emit(g,&ex,8+i));
    }
    op.op=GFX2_OP_FILL; op.rop=0x5a;
    CHECK(!gf_backend_v2.emit(g,&ex,14));
    op.op=GFX2_OP_COPY;
    CHECK(!gf_backend_v2.emit(g,&ex,15));
    CHECK(!d.priv[0] && !s.priv[0]);
    CHECK(!gf_vram_release(&ch, &backing.value));
    CHECK(gf_backend_v2.emit(g,&ex,16)==OS32_ERR_IO);
    CHECK(!gf_vram_reserve(&ch, &backing, 0, 4096, &backing));
    CHECK(!gf_backend_v2.emit(g,&ex,17));
    count=0; s.bytes=4097;
    CHECK(gf_backend_v2.emit(g,&ex,18)==OS32_ERR_IO && !count);
    s.bytes=2048; d.bytes=4097;
    CHECK(gf_backend_v2.emit(g,&ex,19)==OS32_ERR_IO && !count);
    d.bytes=2048;
    /* Contained subranges borrow the reservation, not just exact offsets. */
    d.gpu_off=s.gpu_off=64;
    CHECK(!gf_backend_v2.emit(g,&ex,20));
    d.flags=0;
    CHECK(gf_backend_v2.emit(g,&ex,21)==OS32_ERR_IO);
    d.priv[0]=backing.value.reservation_id; d.priv[1]=backing.value.generation;
    CHECK(!gf_backend_v2.emit(g,&ex,22));
    alias_test = 0;
    const int errors[]={GF2_ETIME,GF2_ENOSPC,GF2_EINVAL,GF3_EINVAL,GF3_ERANGE,GF3_EFORMAT,GF3_EUNSUPPORTED,GF2_EFAULT};
    const int expected[]={OS32_ERR_TIMEOUT,OS32_ERR_NOSPC,OS32_ERR_INVAL,OS32_ERR_INVAL,OS32_ERR_INVAL,OS32_ERR_INVAL,OS32_ERR_NOSYS,OS32_ERR_IO};
    for (u32 i=0;i<sizeof(errors)/sizeof(errors[0]);i++) CHECK(gf_v2_rc(errors[i])==expected[i]);
    io_rc=GF2_ETIME; CHECK(gf_backend_v2.fence_emit(g,7,3)==OS32_ERR_TIMEOUT); io_rc=0;
    CHECK(!gf_backend_v2.stop(g) && gf_v2_inst.lost);
    CHECK(gf_backend_v2.check(g,&ex)==OS32_ERR_NOSYS);
    gf_v2_stopped(); CHECK(gart_calls==1); ch.state=GF2_OFF;
    gf_v2_stopped(); CHECK(gart_calls==2 && clock_calls>0);
    io_rc=GF2_ETIME; CHECK(!gf_backend_v2.stop(g) && gart_calls==2);
    die(0);
}
