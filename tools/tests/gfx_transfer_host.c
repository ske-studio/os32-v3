#include "../../userland/lib/gfx/gfx_direct_internal.h"

static void finish(int rc) __attribute__((noreturn));
static void finish(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg)-1) : "memory"); finish(1); \
} } while (0)
KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_ready = 1, gfx_direct, gfx_lost, gfx_packed, gfx_dirty_suppress;
u16 gfx_gen = 1;
static u8 heap[4000000];
static u32 allocated, alloc_calls;
static int outstanding, fail_alloc, fail_get;
static void *alloc(u32 n)
{
    alloc_calls++;
    if (fail_alloc) return 0;
    CHECK(allocated + n < sizeof(heap));
    void *p = heap + allocated;
    allocated += (n + 3) & ~3U;
    outstanding++;
    return p;
}
static void release(void *p) { CHECK(p != 0); outstanding--; }
static GFX_Rect dirty_rect;
static u32 dirty_count;
static void dirty(int x, int y, int w, int h)
{ dirty_rect = (GFX_Rect){x, y, w, h}; dirty_count++; }
static void check_dirty(int x, int y, int w, int h)
{
    CHECK(dirty_count == 1);
    CHECK(dirty_rect.x == x && dirty_rect.y == y);
    CHECK(dirty_rect.w == w && dirty_rect.h == h);
}
static GFX_Cmd seen[128];
static u8 data[16000], display[640*24], expected[640*24], planar[4][80*24];
static u32 count, bytes, gets;
static int submit(const GFX_Cmd *cmd, u32 n, u32 gen, u32 *done)
{
    CHECK(gen == 1);
    for (u32 i = 0; i < n; i++) {
        GFX_Cmd c = cmd[i];
        CHECK(count < 128 && c.op == GFX_CMD_PUT);
        seen[count++] = c;
        const u8 *pixels = (const u8 *)c.data;
        for (int y = 0; y < c.h; y++) {
            for (int x = 0; x < c.w; x++) {
                u8 v = pixels[y*c.pitch+x];
                CHECK(bytes < sizeof(data));
                data[bytes++] = v;
                if (v != c.key) display[(c.y+y)*640+c.x+x] = v;
            }
        }
    }
    *done = n;
    return 0;
}
static int get(int x, int y, int w, int h, u8 *dst, u32 pitch, u32 gen)
{
    CHECK(gen == 1 && count > 0); /* queued PUT must precede synchronous GET */
    gets++;
    if (fail_get) return OS32_ERR_IO;
    for (int r=0;r<h;r++) for(int c=0;c<w;c++) dst[r*pitch+c]=display[(y+r)*640+x+c];
    return 0;
}
static void reset(int direct, int packed)
{
    gfx_cmd_drop(); gfx_lost=0; count=bytes=gets=dirty_count=0;
    gfx_direct=direct; gfx_packed=packed;
    gfx_fb=(GFX_Framebuffer){.width=640,.height=24,.pitch=packed?640:80};
    for(int p=0;p<4;p++) gfx_fb.planes[p]=direct?0:(packed?display:planar[p]);
    for(u32 i=0;i<sizeof(display);i++) display[i]=9;
    for(int p=0;p<4;p++) for(u32 i=0;i<sizeof(planar[p]);i++) planar[p][i]=(9&(1<<p))?255:0;
}
static u8 pixel(int x,int y)
{
    if(gfx_packed) return display[y*640+x];
    u8 c=0;
    for(int p=0;p<4;p++) if(planar[p][y*80+x/8]&(0x80>>(x&7))) c|=1<<p;
    return c;
}
static void same(void)
{
    for(int y=0;y<24;y++) for(int x=0;x<640;x++) CHECK(pixel(x,y)==expected[y*640+x]);
}
void _start(void)
{
    static KernelAPI api;
    api.mem_alloc=alloc; api.mem_free=release; api.gfx_add_dirty_rect=dirty;
    api.gfx_hw_submit=submit; api.gfx_hw_get=get; gfx_api=&api;
    gfx_surface_init();
    GFX_Surface *s=gfx_create_surface(24,10);
    CHECK(s);
    for(int y=0;y<10;y++) for(int x=0;x<24;x++) gfx_surface_pixel(s,x,y,(x+y)&15);
    GFX_Rect rect={3,1,13,4};
    for(int mode=0;mode<3;mode++) {
        reset(1,1);
        if(mode==0) gfx_blit(5,2,s,&rect);
        if(mode==1) gfx_blit_colorkey(5,2,s,&rect,7);
        if(mode==2) gfx_blit_transparent(5,2,s,&rect);
        check_dirty(5,2,13,4);
        CHECK(!gfx_flush() && count==1);
        CHECK(seen[0].op==GFX_CMD_PUT && seen[0].x==5 && seen[0].y==2);
        CHECK(seen[0].w==13 && seen[0].h==4 && seen[0].pitch==13);
        CHECK(seen[0].key==(mode==0?-1:mode==1?7:0));
        for(int y=0;y<4;y++) for(int x=0;x<13;x++) CHECK(data[y*13+x]==((x+y+4)&15));
        for(u32 i=0;i<sizeof(display);i++) expected[i]=display[i];
        for(int packed=0;packed<2;packed++) {
            reset(0,packed);
            if(mode==0) gfx_blit(5,2,s,&rect);
            if(mode==1) gfx_blit_colorkey(5,2,s,&rect,7);
            if(mode==2) gfx_blit_transparent(5,2,s,&rect);
            CHECK(count==0); same();
        }
    }
    reset(1,1); gfx_blit_colorkey(1,1,s,0,255); CHECK(!gfx_flush() && seen[0].key==255);
    reset(1,1); gfx_blit(-3,-2,s,0); CHECK(!gfx_flush() && count==1);
    CHECK(seen[0].x==0 && seen[0].y==0 && seen[0].w==21 && seen[0].h==8 && data[0]==5);
    check_dirty(-3,-2,24,10);
    for(int mode=0;mode<3;mode++) {
        reset(1,1); gfx_dirty_suppress=1;
        if(mode==0) gfx_blit(5,2,s,&rect);
        if(mode==1) gfx_blit_colorkey(5,2,s,&rect,7);
        if(mode==2) gfx_blit_transparent(5,2,s,&rect);
        CHECK(!dirty_count && !gfx_flush() && count==1);
        gfx_dirty_suppress=0;
    }
    /* Conversion bundles and the queue both fit complete rows in 4KiB. */
    GFX_Surface *large=gfx_create_surface(640,10); CHECK(large);
    gfx_surface_clear(large,6);
    reset(1,1); gfx_blit(0,0,large,0); CHECK(!gfx_flush() && count==2);
    check_dirty(0,0,640,10);
    CHECK(seen[0].h==6 && seen[1].y==6 && seen[1].h==4 && bytes==6400);
    GFX_Sprite *spr=gfx_create_sprite(s,7); CHECK(spr);
    reset(1,1); gfx_draw_sprite(5,2,spr); CHECK(!gfx_flush() && count==1);
    check_dirty(5,2,24,10);
    CHECK(seen[0].w==24 && seen[0].h==10 && seen[0].x==5 && seen[0].y==2 && seen[0].key==255);
    for(int y=0;y<10;y++) for(int x=0;x<24;x++) CHECK(data[y*24+x]==(((x+y)&15)==7?255:((x+y)&15)));
    for(u32 i=0;i<sizeof(display);i++) expected[i]=display[i];
    for(int packed=0;packed<2;packed++) {reset(0,packed);gfx_draw_sprite(5,2,spr);check_dirty(5,2,24,10);same();}
    for(int angle=0;angle<512;angle+=64) for(int scale=128;scale<=512;scale*=2) {
        reset(1,1);gfx_blit_affine(8,4,s,angle,scale,0);
        CHECK(!gfx_flush() && count==9);
        for(u32 i=0;i<count;i++) CHECK(seen[i].x==0 && seen[i].y==(int)i && seen[i].w==20 && seen[i].h==1 && seen[i].key==0);
        for(u32 i=0;i<sizeof(display);i++) expected[i]=display[i];
        for(int packed=0;packed<2;packed++) {reset(0,packed);gfx_blit_affine(8,4,s,angle,scale,0);same();}
    }
    /* Save/restore the byte-aligned INDEX4 backing store, including edge bits. */
    static u8 bg[4*4*10];
    reset(1,1);gfx_draw_sprite(5,2,spr);gfx_sprite_save_bg(5,2,spr);
    CHECK(gets==1 && count==1);
    for(u32 i=0;i<sizeof(display);i++) expected[i]=display[i];
    dirty_count=0;
    gfx_sprite_restore_bg(5,2,spr);check_dirty(5,2,24,10);CHECK(!gfx_flush() && count==2);
    for(int r=0;r<10;r++) for(int c=0;c<32;c++)
        CHECK(data[240+r*32+c]==expected[(r+2)*640+c]);
    CHECK(seen[1].x==0 && seen[1].w==32 && seen[1].h==10 && seen[1].key==-1);
    for(int packed=0;packed<2;packed++) {
        reset(0,packed);gfx_blit(5,2,s,0);gfx_save_rect(5,2,24,10,bg);
        for(u32 i=0;i<sizeof(display);i++) expected[i]=pixel(i%640,i/640);
        gfx_surface_clear(s,0);gfx_blit(5,2,s,0);gfx_restore_rect(5,2,24,10,bg);same();
    }
    reset(1,1);gfx_blit(0,0,large,0); fail_get=1;
    gfx_save_rect(0,0,8,1,bg);CHECK(gets==1 && gfx_lost);fail_get=0;
    int before=outstanding;
    reset(1,1); fail_alloc=1;
    u32 calls=alloc_calls;
    gfx_blit(0,0,s,0); gfx_draw_sprite(0,0,spr);
    CHECK(!gfx_flush() && count==2 && !gfx_lost && alloc_calls==calls);
    gfx_save_rect(0,0,8,1,bg); CHECK(gfx_lost && gets==0);
    reset(1,1); gfx_restore_rect(0,0,8,1,bg); CHECK(gfx_lost);
    CHECK(outstanding==before && outstanding==3); fail_alloc=0;
    /* Wider/taller than the screen: source offsets, complete row bundles, no heap. */
    GFX_Surface *oversize=gfx_create_surface(800,20); CHECK(oversize);
    for(int y=0;y<20;y++) for(int x=0;x<800;x++) gfx_surface_pixel(oversize,x,y,(x+3*y)&15);
    reset(1,1); gfx_fb.height=12; calls=alloc_calls; gfx_blit(-5,-3,oversize,0);
    CHECK(!gfx_flush() && count==2 && bytes==640*12 && alloc_calls==calls);
    for(u32 i=0;i<count;i++) CHECK(seen[i].x==0 && seen[i].y==(int)i*6 && seen[i].w==640 && seen[i].h==6);
    for(int y=0;y<12;y++) for(int x=0;x<640;x++) CHECK(display[y*640+x]==((x+5+3*(y+3))&15));
    GFX_Sprite *big_spr=gfx_create_sprite(oversize,6); CHECK(big_spr);
    reset(1,1); gfx_fb.width=32; gfx_fb.height=8; calls=alloc_calls;
    gfx_draw_sprite(-3,-2,big_spr); CHECK(!gfx_flush() && count==1 && bytes==256 && alloc_calls==calls);
    check_dirty(-3,-2,800,20);
    CHECK(seen[0].x==0 && seen[0].y==0 && seen[0].w==32 && seen[0].h==8);
    for(int y=0;y<8;y++) for(int x=0;x<32;x++) {
        u8 c=(x+3+3*(y+2))&15; CHECK(data[y*32+x]==(c==6?255:c));
    }
    /* Unsupported row widths are rejected by enqueue and must leave loss recorded. */
    reset(1,1); gfx_fb.width=800; gfx_blit(0,0,oversize,0); CHECK(gfx_lost);
    reset(1,1); gfx_fb.width=800;
    GFX_Sprite invalid=*big_spr; invalid.w=700; invalid.pitch=oversize->pitch;
    static u8 invalid_mask[2000]; invalid.mask=invalid_mask;
    for(int p=0;p<4;p++) invalid.planes[p]=oversize->planes[p];
    gfx_draw_sprite(0,0,&invalid); CHECK(gfx_lost);
    for(int packed=0;packed<2;packed++) {
        static u8 backing[4*80*10];
        reset(0,packed); gfx_fb.planes[0]=0;
        gfx_blit(1,1,s,0); gfx_blit_colorkey(1,1,s,0,7); gfx_blit_transparent(1,1,s,0);
        gfx_draw_sprite(1,1,spr); gfx_save_rect(0,0,24,10,backing);
        gfx_restore_rect(0,0,24,10,backing); gfx_sprite_save_bg(0,0,spr);
        CHECK(!count && !dirty_count);
        gfx_sprite_restore_bg(0,0,spr); CHECK(!count && dirty_count==1);
    }
    finish(0);
}
