#include "gf_command_runtime_host.h"
#include "../../kdebug/gf_prim_bench.c"
static struct gf_foundation f;
static struct gf2_chan ch;
static struct gf3_ctx ctx;
static struct gf3_surface surf;
static struct gf3_test_ops ops;
static struct gf1_test_ops fops;
static u32 current, calls, clocks, ticks, fail_at, allocs, frees;
static bool no_memory;
static u8 buffer[65536] __attribute__((aligned(4096)));
u32 pgalloc_alloc_phys(u32 owner, int n)
{
    assert(owner==LEDGER_OWNER_KERNEL && n==16); ++allocs;
    return no_memory ? 0 : (u32)buffer;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    assert(owner==LEDGER_OWNER_KERNEL && n==16 && pfn==(u32)buffer/4096);
    ++frees; return 1; /* real pgalloc_free_n_owner returns 1 on success */
}
static char output[16384];
static u32 used;
void serial_puts_polled(const char *s)
{
    while (*s) { assert(used+1 < sizeof(output)); output[used++]=*s++; }
    output[used]=0;
}
const struct gf3_test_ops *geforce_accel_ops(void) { return &ops; }
const struct gf1_test_ops *geforce_foundation_ops(void) { return &fops; }
static int time_read(void *p,u32 *n)
{ assert(p==&f); ++clocks; *n=ticks++; return 0; }
static int hit(void)
{ ++calls; ticks+=2; return fail_at==calls ? GF2_EIO : 0; }
static u32 expected_item(void) { return current ? current : calls/5100+1; }
static int fill(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
                u32 color,u8 alu,u32 mask,u32 dl)
{
    assert(c==&ctx && s->off==surf.off && s->reservation_id==surf.reservation_id);
    assert(color==0x5a && alu==3 && mask==0xffffffffU && dl>=GF3_TIMEOUT_US);
    assert(r.x==0 && r.y==0 && r.w==(expected_item()==1?1:surf.width));
    assert(r.h==(expected_item()==1?1:surf.height));
    return hit();
}
static int copy(struct gf3_ctx *c,const struct gf3_surface *s,const struct gf3_surface *d,
                struct gf3_rect r,u32 x,u32 y,u8 alu,u32 mask,u32 dl)
{
    assert(c==&ctx && s->off==surf.off && d->off==surf.off && !x);
    assert(r.w==surf.width && r.h==surf.height/2 && y==r.h);
    assert(alu==3 && mask==0xffffffffU && dl>=GF3_TIMEOUT_US);
    return hit();
}
static int mono(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
                const struct gf3_ram *ram,u32 fg,u32 bg,struct gf3_rect clip,u32 dl)
{
    assert(c==&ctx && s->off==surf.off && fg==0xe3 && bg==0x19);
    assert(r.w==256 && r.h==32 && ram->bytes==1024 && ram->pitch==32);
    assert(clip.w==r.w && clip.h==r.h && dl>=GF3_TIMEOUT_US);
    return hit();
}
static int upload(struct gf3_ctx *c,const struct gf3_surface *s,struct gf3_rect r,
                  const struct gf3_ram *ram,bool force,u32 dl)
{
    assert(c==&ctx && s->off==surf.off && force==(expected_item()==6));
    assert(r.w==256 && r.h==256 && ram->bytes==65536 && ram->pitch==256);
    assert(dl>=GF3_TIMEOUT_US && ((u8*)ram->ptr)[1]==37);
    return hit();
}
static int read_bar(void *p,u32 bar,u32 off,u32 width,u32 *v)
{
    assert(p==&f && !bar && !off && width==4); *v=0xdeadbeef; return hit();
}
static void setup(void)
{
    surf=(struct gf3_surface){.off=0x200000,.bytes=640*480,.pitch=640,
        .width=640,.height=480,.fmt=GF3_INDEX8,.reservation_id=42,.generation=7};
    f.io.opaque=&f; f.io.timing.clock_us=time_read; f.io.read=read_bar;
    fops.production=&f;
    ch.f=&f; ch.epoch=7; ch.state=GF2_RUN; ch.fault.latched=false;
    ctx.ch=&ch; ctx.epoch=7; ctx.ready=true;
    ops=(struct gf3_test_ops){.ctx=&ctx,.test_surface=&surf,.fill=fill,
        .copy=copy,.mono=mono,.upload=upload};
    calls=clocks=ticks=fail_at=used=allocs=frees=0; no_memory=false; output[0]=0;
}
void _start(void)
{
    for(current=1;current<=7;current++) {
        setup(); assert(!gf_prim_bench(current)); assert(calls==5100);
        assert(allocs==(current>=4 && current<=6) && frees==allocs);
        assert(gf2_host_strstr(output,"GFP1 done rc=0\r\n"));
        assert(!gf2_host_strstr(output," 0 301 "));
        assert(gf2_host_strstr(output,current==7?" 50 201 2010 0":" 50 301 3010 0"));
        if(current==3) assert(gf2_host_strstr(output,"measured=half derived_full=6020ns"));
        if(current==7) assert(clocks==102);
    }
    setup(); current=0;
    assert(!gf_prim_bench(0) && calls==35700 && allocs==1 && frees==1);
    assert(gf2_host_strstr(output,"GFP1 bar0 50 201 2010 0"));
    setup(); current=2; fail_at=3; assert(gf_prim_bench(2)==GF2_EIO && calls==3);
    assert(gf2_host_strstr(output,"GFP1 done rc=-"));
    setup(); current=5; fail_at=3;
    assert(gf_prim_bench(5)==GF2_EIO && allocs==1 && frees==1);
    setup(); no_memory=true;
    assert(gf_prim_bench(5)==GF2_ENOSPC && !calls && !frees);
    setup(); ctx.epoch++; assert(gf_prim_bench(1)==GF3_ESTATE && !calls);
    setup(); current=1; ch.fault.latched=true; assert(gf_prim_bench(1)==GF3_ESTATE && !calls);
    setup(); assert(gf_prim_bench(8)==GF3_EINVAL && !calls);
    setup(); current=3; surf.width=320; surf.height=240;
    assert(!gf_prim_bench(3) && calls==5100);
    assert(gf2_host_strstr(output,"w=320 h=240"));
    gf2_host_exit(0);
}
