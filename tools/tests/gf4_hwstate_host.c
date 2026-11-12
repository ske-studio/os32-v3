/* GF_STOP_REF_DESIGN §5-2: independent register/plane oracle, real save/load. */
#define GF4_HOST_UNIT 14
#include "gf4_display_fake.h"
#include "../../hwdev/gf/display/hw_state.h"
#include "../../mm/pgalloc.h"
static struct gf4_host h;
static struct gf4_state state;
static struct gf4_host_event events[20000];
static struct gf3_host_reg regs[2048];
static u32 pages[65536] __attribute__((aligned(4096)));
static u32 planes[4][16384],reads,writes,allocs,frees;
static bool alloc_fail;
u32 pgalloc_alloc_phys(u32 owner,int n)
{ assert(owner==LEDGER_OWNER_KERNEL && n==64); ++allocs; return alloc_fail ? 0 : V2P(pages); }
int pgalloc_free_n_owner(u32 owner,u32 pfn,int n)
{ assert(owner==LEDGER_OWNER_KERNEL && pfn==V2P(pages)/PAGE_SIZE && n==64); ++frees; return 1; }
void __cdecl kprintf(u8 attr,const char *fmt,...) { (void)attr;(void)fmt; }
static int read_io(void *o,u32 bar,u32 off,u32 width,u32 *v)
{
    if (bar==GF_MODE_VRAM_BAR) {
        assert(width==4 && off<65536); ++reads;
        *v=planes[h.gr[0][4]][off/4]; return 0;
    }
    return gf4_host_read(o,bar,off,width,v);
}
static int write_io(void *o,u32 bar,u32 off,u32 width,u32 v)
{
    if (bar==GF_MODE_VRAM_BAR) {
        assert(width==4 && off<65536); ++writes;
        for (u32 p=0;p<4;++p) if (h.sr[0][2]&(1U<<p)) planes[p][off/4]=v;
        return 0;
    }
    return gf4_host_write(o,bar,off,width,v);
}
static struct gf_io io;
static void fixture(void)
{
    gf4_hw_state_reset();
    gf4_host_init(&h,&state); h.events=events; h.cevents=20000;
    h.base.regs=regs; h.base.cregs=2048;
    io=(struct gf_io){.opaque=&h,.read=read_io,.write=write_io};
    reads=writes=allocs=frees=0; alloc_fail=false;
    for (u32 p=0;p<4;++p) for (u32 i=0;i<16384;++i) planes[p][i]=(p<<24)^i;
    for (u32 head=0;head<2;++head) {
        h.cr[head][0x17]=0; h.cr[head][0x1a]=head ? 0xff : 0x3f;
        h.cr[head][0x21]=0xfa; h.sr[head][1]=0x20;
        gf3_host_set(&h.base,0,0x680508+head*0x18,0x51104);
        gf3_host_set(&h.base,0,head ? 0x68057c : 0x680578,0x80000e02);
    }
    gf3_host_set(&h.base,0,0x68052c,1); gf3_host_set(&h.base,0,0x68252c,0x101);
    gf3_host_set(&h.base,0,0x10020c,0x8000000);
}
static u32 word(u32 off) { u32 v; assert(!read_io(&h,0,off,4,&v)); return v; }
static u32 at(u32 off,u32 start)
{
    for (u32 i=start;i<h.nevents;++i) if (events[i].op==D_WRITE && events[i].a[1]==off) return i;
    assert(0); return 0;
}
static u32 indexed(u32 port,u32 index,u32 value,u32 start)
{
    for (u32 i=start;i+1<h.nevents;++i)
        if (events[i].op==D_WRITE && events[i].a[1]==port && events[i].a[3]==index &&
            events[i+1].op==D_WRITE && events[i+1].a[1]==port+1 && events[i+1].a[3]==value) return i;
    assert(0); return 0;
}
static void pll(void)
{
    for (u32 head=0;head<2;++head) {
        fixture(); u32 r1=head ? 0x680520 : 0x680508,r2=head ? 0x68057c : 0x680578;
        gf3_host_set(&h.base,0,r1,0x21a07); gf3_host_set(&h.base,0,r2,0x8000011f);
        gf3_host_set(&h.base,0,0xc040,0xffffffff);
        struct gf4_hw_pllvals pv={.nm1=0x1104,.nm2=0xe02,.log2p=5};
        assert(!gf4_hw_pll_restore(&io,head,&pv));
        u32 a=at(0xc040,0),b=at(r2,a+1),c=at(r1,b+1),d=at(0xc040,c+1);
        assert(events[a].a[3]==(0xffffffffU&~(3U<<(head ? 18 : 16))));
        assert(events[d].a[3]==0xffffffffU && word(r1)==0x51104 && word(r2)==0x80000e02);
        for (u32 i=0;i<h.nevents;++i) assert(events[i].a[1]!=0x1584);
        gf4_host_trace_clear(&h); assert(!gf4_hw_pll_restore(&io,head,&pv)); assert(!h.calls[D_WRITE]);
    }
}
static void save_load(void)
{
    fixture(); struct gf4_route routes[2]={{.type=GF4_ANALOG,.or_mask=1},{.type=GF4_ANALOG,.or_mask=2}};
    struct gf4_ctx c={0}; c.dcb.routes[0]=routes[0]; c.dcb.routes[1]=routes[1]; c.dcb.nroutes=2;
    c.limits[0].ref=c.limits[1].ref=27000;
    gf4_hw_save(&c,&io); struct gf4_hw_saved *s=gf4_hw_saved_get();
    assert(s->valid && s->fonts_valid && reads==65536 && allocs==1);
    assert(s->nroutes==2 && s->output[0].output==1 && s->output[1].output==0x101);
    assert(!(s->crtc[1].sr[1]&0x20));
    u32 n=h.nevents; gf4_hw_save(&c,&io); assert(h.nevents==n && allocs==1);
    for (u32 head=0;head<2;++head) {
        s->crtc[head].sr[1]|=0x20;
        gf4_host_trace_clear(&h); h.sr[head][1]=0x20; h.cr[head][0x21]=0;
        assert(!gf4_hw_crtc_restore(&io,head,s));
        assert(h.cr[0][0x44]==(head ? 3 : 0));
        assert(!(h.sr[head][1]&0x20) && (h.cr[head][0x11]&0x80));
        assert(!h.cr[head][0x17] && h.cr[head][0x1a]==(head ? 0xff : 0x3f) && h.cr[head][0x21]==0xfa);
        u32 cursync=at(0x680404+head*0x2000,0);
        u32 select=at(0x68050c,cursync+1);
        u32 engine=at(0x600860+head*0x2000,select+1),video=at(0x8704,engine+1);
        assert(video>engine && word(0x1588)==0 && word(0x8908)==0x7ffffff);
        assert(indexed(0x6013d4+head*0x2000,0x33,s->crtc[head].cr[0x33],0)>engine);
        u32 palette=at(0x6813c6+head*0x2000,video+1);
        u32 vga=at(0xc03c2+head*0x2000,palette+1);
        u32 unlock=indexed(0x6013d4+head*0x2000,0x11,0,vga+1);
        assert(indexed(0x6013d4+head*0x2000,0,s->crtc[head].cr[0],unlock+1)>unlock);
        u32 unprotect=indexed(0xc03c4+head*0x2000,1,0,unlock+1);
        assert(indexed(0x6013d4+head*0x2000,0x21,0xfa,unprotect+1)>unprotect);
        at(0x680300+head*0x2000,video+1); /* nv_fix_nv40_hw_cursor */
        assert(!(word(0x680900+head*0x2000)&0x10000));
        s->crtc[head].crtc_cfg=2; gf4_host_trace_clear(&h);
        assert(!gf4_hw_crtc_restore(&io,head,s));
        assert(word(0x680900+head*0x2000)&0x10000);
    }
    for (u32 p=0;p<4;++p) for (u32 i=0;i<16384;++i) planes[p][i]=0;
    assert(!gf4_hw_fonts_restore(&io,s) && writes==65536);
    for (u32 p=0;p<4;++p) for (u32 i=0;i<16384;++i) assert(planes[p][i]==((p<<24)^i));
    gf4_hw_state_reset(); assert(frees==1 && !s->valid && !s->fonts_valid);
    fixture(); h.ar[0][0x10]=1; gf4_hw_save(&c,&io);
    assert(s->valid && !s->fonts_valid && !reads && !allocs);
    fixture(); alloc_fail=true; gf4_hw_save(&c,&io); assert(s->valid && !s->fonts_valid);
    fixture(); gf4_host_fail_at(&h,D_READ,0,GF2_EIO); gf4_hw_save(&c,&io); assert(!s->valid);
}
void _start(void)
{
    pll(); save_load();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(0)); __builtin_unreachable();
}
