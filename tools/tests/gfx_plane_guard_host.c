#include "gfx_direct_internal.h"
#include "libgfx_internal.h"
#include "md_render.h"
#include "tilemap_internal.h"
#include <string.h>

static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char m[]="FAIL " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(m), "d"(sizeof(m)-1) : "memory"); finish(1); } } while (0)
GFX_Framebuffer gfx_fb;
int gfx_ready=1, gfx_direct, gfx_packed, gfx_dirty_suppress, kcg_scale=1;
TilemapState _tilemap;
static int draws;
void *memset(void *d,int c,size_t n) { u8 *p=d; while(n--) *p++=c; return d; }
void *memcpy(void *d,const void *s,size_t n) { u8 *p=d; const u8 *q=s; while(n--) *p++=*q++; return d; }
void *memmove(void *d,const void *s,size_t n) { u8 *p=d; const u8 *q=s; CHECK(d && s); if(p<q) while(n--) *p++=*q++; else while(n) { n--; p[n]=q[n]; } return d; }
void gfx_fill_rect(int x,int y,int w,int h,u8 c) { (void)x;(void)y;(void)w;(void)h;(void)c; draws++; }
void gfx_hline(int x,int y,int w,u8 c) { (void)x;(void)y;(void)w;(void)c; draws++; }
void gfx_vline(int x,int y,int h,u8 c) { (void)x;(void)y;(void)h;(void)c; draws++; }
int kcg_draw_utf8(int x,int y,const char *s,u8 f,u8 b) { (void)x;(void)y;(void)s;(void)f;(void)b; draws++; return 0; }
void kcg_set_scale(int s) { kcg_scale=s; }
void gfx_blit(int x,int y,const GFX_Surface *s,const GFX_Rect *t) { (void)x;(void)y;(void)s;(void)t; draws++; }
void gfx_blit_colorkey(int x,int y,const GFX_Surface *s,const GFX_Rect *t,u8 k) { (void)x;(void)y;(void)s;(void)t;(void)k; draws++; }
void gfx_blit_transparent(int x,int y,const GFX_Surface *s,const GFX_Rect *t) { (void)x;(void)y;(void)s;(void)t; draws++; }
void asm_gfx_hline(u8 **p,int b,int x,int x2,u8 c) { (void)p;(void)b;(void)x;(void)x2;(void)c; draws++; }
void _start(void) {
    static KernelAPI api;
    static MdDocument doc;
    static u8 planes[4][80*MDR_PAGE_H];
    gfx_fb.width=640; gfx_fb.height=400; gfx_fb.pitch=80;
    for(int p=0;p<4;p++) gfx_fb.planes[p]=planes[p];
    md_render_init(&api);
    gfx_direct=1;
    md_render_page(&doc,0); md_render_page(&doc,1); CHECK(draws==0);
    for(int p=0;p<4;p++) gfx_fb.planes[p]=0;
    md_render_page(&doc,2); CHECK(draws==0);
    gfx_direct=0; md_render_page(&doc,3); CHECK(draws==0);
    /* A rejected DIRECT page must not advance the SHADOW scroll history. */
    for(int p=0;p<4;p++) gfx_fb.planes[p]=planes[p];
    md_render_page(&doc,3); CHECK(draws==1);
    for(int p=0;p<4;p++) for(int y=0;y<MDR_PAGE_H;y++) planes[p][y*80]=(u8)y;
    md_render_page(&doc,4); CHECK(draws==2);
    for(int p=0;p<4;p++) CHECK(planes[p][0]==1 && planes[p][80]==2);
    for(int p=0;p<4;p++) gfx_fb.planes[p]=0;
    for(int packed=0;packed<2;packed++) {
        gfx_packed=packed; int before=draws;
        gfx_span_raw(0,0,7,1); gfx_p_put(1,1,1); CHECK(gfx_p_get(1,1)==0 && draws==before);
    }
    static BGPlane bg[BG_COUNT];
    _tilemap.kapi=&api; _tilemap.bg_planes=bg;
    void (*compose[])(void)={tilemap_compose_btf,tilemap_compose_ftb,tilemap_compose_btf_fast,tilemap_compose_scroll};
    for(int mode=0;mode<2;mode++) for(int i=0;i<4;i++) {
        gfx_direct=mode; bg[0].dirty[0][0]=1; _tilemap.scroll_changed=1;
        compose[i](); CHECK(bg[0].dirty[0][0]==1 && _tilemap.scroll_changed==1);
    }
    finish(0);
}
