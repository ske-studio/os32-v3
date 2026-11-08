#include "gfx_direct_internal.h"
#include "gfx_font.h"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) { __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
#define CHECK(x) do { if (!(x)) { static const char m[] = "FAIL " #x "\n"; __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(m), "d"(sizeof(m)-1) : "memory"); finish(1); } } while (0)
KernelAPI *gfx_api;
int gfx_direct, gfx_lost;
u16 gfx_gen = 1;
GFX_Framebuffer gfx_fb = {.width=640, .height=480};
static GFX_Cmd cmds[32]; static u8 data[4096]; static int n, used, fonts, fills, pixels;
static int submit(const GFX_Cmd *c, u32 count, u32 gen, u32 *done) {
    CHECK(gen == 1);
    for (u32 i=0; i<count; i++) { cmds[n++] = c[i]; for (u32 j=0; j<(u32)c[i].h*c[i].pitch; j++) data[used++] = ((u8 *)c[i].data)[j]; }
    *done=count; return 0;
}
void gfx_draw_font(int x,int y,const u8 *p,int w,int h,u8 fg) { (void)x;(void)y;(void)p;(void)w;(void)h;(void)fg; fonts++; }
void gfx_fill_rect(int x,int y,int w,int h,u8 c) { (void)x;(void)y;(void)w;(void)h;(void)c; fills++; }
void gfx_pixel(int x,int y,u8 c) { (void)x;(void)y;(void)c; pixels++; }
static void ank(u8 c,u8 *p) { for (int i=0;i<16;i++) p[i]=c; }
static void kanji(u16 c,u8 *p) { (void)c; for (int i=0;i<32;i++) p[i]=(i&1)?0x40:0x80; }
typedef struct { u32 codepoint; int bytes_used; } utf8_decode_t;
utf8_decode_t utf8_decode(const u8 *s) { utf8_decode_t d = {*s,1}; return d; }
u8 unicode_to_ank(u32 cp) { return cp == 'A' ? 0x80 : 0; }
u16 unicode_to_jis(u32 cp) { (void)cp; return 0x2121; }
void kcg_draw_ank(int,int,u8,u8,u8); void kcg_set_scale(int); int kcg_draw_utf8(int,int,const char *,u8,u8);
void _start(void) {
 static KernelAPI api; api.gfx_hw_submit=submit; api.kcg_read_ank=ank; api.kcg_read_kanji=kanji; gfx_api=&api;
 gfx_puts(4,5,"AB\nA",7); CHECK(fonts==3 && n==0);
 kcg_draw_ank(0,0,0x80,1,2); CHECK(fonts==4 && fills==1);
 kcg_set_scale(2); kcg_draw_ank(0,0,0x80,1,0xff); CHECK(pixels==64);
 gfx_direct=1; gfx_puts(4,5,"AB\nA",7); CHECK(!gfx_flush());
 CHECK(n==2 && cmds[0].op==GFX_CMD_MONO && cmds[0].x==4 && cmds[0].y==5 && cmds[0].w==16 && cmds[0].h==8 && cmds[0].color==7 && cmds[0].key==-1);
 CHECK(data[0]==0x18 && data[1]==0x7c && data[2]==0x3c && data[3]==0x66);
 CHECK(cmds[1].x==4 && cmds[1].y==14 && cmds[1].w==8);
 n=used=0; kcg_set_scale(1); CHECK(kcg_draw_utf8(10,20,"AB",3,4)==24); CHECK(!gfx_flush());
 CHECK(n==1 && cmds[0].op==GFX_CMD_MONO && cmds[0].x==10 && cmds[0].y==20 && cmds[0].w==24 && cmds[0].h==16 && cmds[0].color==3 && cmds[0].key==4);
 CHECK(data[0]==0x80 && data[1]==0x80 && data[2]==0x40);
 n=used=0; kcg_set_scale(2); kcg_draw_ank(1,2,0x80,5,0xff); CHECK(!gfx_flush());
 CHECK(n==1 && cmds[0].w==16 && cmds[0].h==32 && cmds[0].key==-1 && data[0]==0xc0 && data[1]==0 && data[2]==0xc0);
 finish(0);
}
