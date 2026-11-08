#include "../../userland/lib/gfx/gfx_direct_internal.h"
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char m[] = "FAIL " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(m), "d"(sizeof(m)-1) : "memory"); finish(1); } } while (0)
KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_ready = 1, gfx_packed = 1, gfx_direct = 1, gfx_lost;
u16 gfx_gen = 5;
static GFX_Cmd seen[64];
static int n, dirty, gets;
static int rects[128][4];
static int submit(const GFX_Cmd *c, u32 count, u32 gen, u32 *done) { CHECK(gen == 5); for (u32 i=0;i<count;i++) seen[n++]=c[i]; *done=count; return 0; }
static int get(int x,int y,int w,int h,u8 *d,u32 pitch,u32 gen) { CHECK(x==2 && y==3 && w==1 && h==1 && pitch==1 && gen==5 && n==13); gets++; *d=91; return 0; }
static void dirty_rect(int x,int y,int w,int h) { rects[dirty][0]=x; rects[dirty][1]=y; rects[dirty][2]=w; rects[dirty][3]=h; dirty++; }
void asm_gfx_clear(u8 c,u8 **p) {(void)c;(void)p;}
void asm_fill_plane_rect(u8 *s,int p,int r,int w,u8 c) {(void)s;(void)p;(void)r;(void)w;(void)c;}
void asm_gfx_line(u8 **p,int pitch,int x,int y,int x1,int y1,u8 c) {(void)p;(void)pitch;(void)x;(void)y;(void)x1;(void)y1;(void)c;}
void asm_gfx_hline(u8 **p,int b,int x,int x2,u8 c) {(void)p;(void)b;(void)x;(void)x2;(void)c;}
void gfx_clear_rect(int, int, int, int, u8);
static void check(int i,int x,int y,int w,int h,u32 color) { CHECK(seen[i].op==GFX_CMD_FILL && seen[i].rop==0 && seen[i].x==x && seen[i].y==y && seen[i].w==w && seen[i].h==h && seen[i].color==color && seen[i].data==0); }
void _start(void) {
 static KernelAPI api; static u8 fb[80];
 api.gfx_hw_submit=submit; api.gfx_hw_get=get; api.gfx_add_dirty_rect=dirty_rect; gfx_api=&api;
 gfx_fb.width=10; gfx_fb.height=8; gfx_fb.pitch=10;
 gfx_clear(1); gfx_clear_rect(-1,2,4,3,2); gfx_pixel(2,3,3); gfx_pixel_nodirty(4,5,4);
 gfx_hline(1,2,3,5); gfx_vline(6,1,4,6); gfx_fill_rect(3,2,4,5,7);
 gfx_rect(1,1,3,2,8); gfx_line(0,0,1,1,9);
 CHECK(gfx_get_pixel(2,3)==91 && gets==1 && n==13 && dirty==12);
 check(0,0,0,10,8,1); check(1,-1,2,4,3,2); check(2,2,3,1,1,3); check(3,4,5,1,1,4);
 check(4,1,2,3,1,5); check(5,6,1,1,4,6); check(6,3,2,4,5,7);
 check(7,1,1,3,1,8); check(8,1,2,3,1,8); check(9,1,1,1,2,8); check(10,3,1,1,2,8);
 check(11,0,0,1,1,9); check(12,1,1,1,1,9);
 gfx_line(0,4,9,6,10); gfx_flush(); CHECK(n==16);
 check(13,0,4,3,1,10); check(14,3,5,4,1,10); check(15,7,6,3,1,10);
 gfx_direct=0; gfx_fb.planes[0]=fb; gfx_pixel(2,3,22); CHECK(fb[32]==22 && dirty==14 && gfx_get_pixel(2,3)==22 && n==16);
 {
  static const int ends[][4] = {{0,0,9,2},{9,2,0,0},{0,7,9,4},{9,4,0,7},
    {1,0,3,7},{3,7,1,0},{8,0,6,7},{6,7,8,0},{0,2,9,2},{4,0,4,7},{3,3,3,3}};
  static u8 actual[80];
  for (unsigned int k=0;k<sizeof(ends)/sizeof(ends[0]);k++) {
   int x0=ends[k][0], y0=ends[k][1], x1=ends[k][2], y1=ends[k][3];
   int dx=x1>x0?x1-x0:x0-x1, dy=y1>y0?y1-y0:y0-y1;
   for (int i=0;i<80;i++) fb[i]=actual[i]=0;
   n=dirty=0; gfx_direct=1; gfx_fb.planes[0]=0;
   gfx_line(x0,y0,x1,y1,42); gfx_flush();
   CHECK(n==(dx<dy?dx:dy)+1 && dirty==1);
   for (int i=0;i<n;i++) for (int y=seen[i].y;y<seen[i].y+seen[i].h;y++)
    for (int x=seen[i].x;x<seen[i].x+seen[i].w;x++) actual[y*10+x]=42;
   int rx=rects[0][0], ry=rects[0][1], rw=rects[0][2], rh=rects[0][3];
   gfx_direct=0; gfx_fb.planes[0]=fb; gfx_line(x0,y0,x1,y1,42);
   CHECK(dirty==2 && rx==rects[1][0] && ry==rects[1][1] && rw==rects[1][2] && rh==rects[1][3]);
   for (int i=0;i<80;i++) CHECK(actual[i]==fb[i]);
  }
 }
 finish(0);
}
