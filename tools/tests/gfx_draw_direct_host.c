#include "../../userland/lib/gfx/gfx_direct_internal.h"
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char m[] = "FAIL " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(m), "d"(sizeof(m)-1) : "memory"); finish(1); } } while (0)
KernelAPI *gfx_api;
GFX_Framebuffer gfx_fb;
int gfx_ready = 1, gfx_packed = 1, gfx_direct = 1, gfx_lost;
u16 gfx_gen = 5;
static GFX_Cmd seen[64];
static int n, dirty, gets;
static int submit(const GFX_Cmd *c, u32 count, u32 gen, u32 *done) { CHECK(gen == 5); for (u32 i=0;i<count;i++) seen[n++]=c[i]; *done=count; return 0; }
static int get(int x,int y,int w,int h,u8 *d,u32 pitch,u32 gen) { CHECK(x==2 && y==3 && w==1 && h==1 && pitch==1 && gen==5 && n==13); gets++; *d=91; return 0; }
static void dirty_rect(int x,int y,int w,int h) { (void)x;(void)y;(void)w;(void)h; dirty++; }
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
 CHECK(gfx_get_pixel(2,3)==91 && gets==1 && n==13 && dirty==0);
 check(0,0,0,10,8,1); check(1,-1,2,4,3,2); check(2,2,3,1,1,3); check(3,4,5,1,1,4);
 check(4,1,2,3,1,5); check(5,6,1,1,4,6); check(6,3,2,4,5,7);
 check(7,1,1,3,1,8); check(8,1,2,3,1,8); check(9,1,1,1,2,8); check(10,3,1,1,2,8);
 check(11,0,0,1,1,9); check(12,1,1,1,1,9);
 gfx_direct=0; gfx_fb.planes[0]=fb; gfx_pixel(2,3,22); CHECK(fb[32]==22 && dirty==1 && gfx_get_pixel(2,3)==22 && n==13);
 finish(0);
}
