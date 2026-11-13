/* Synthetic KCG1 payload with asymmetric MSB-left ASCII, kana and JIS masks. */
#include "types.h"
#include "kstring.h"
#include "console.h"
#include "tvram.h"
#include "platform/powermac/pm_console.h"
#include "fbcon_source.inc"
static void finish(int rc) __attribute__((noreturn));
static void finish(int rc) {
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void say(const char *s) {
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(kstrlen(s)) : "memory");
}
#define CHECK(c) do { if (!(c)) { say("FAIL " #c "\n"); finish(1); } } while (0)
static u8 heap[64000], font_heap[296000];
void *kmalloc(u32 n) { if(n>sizeof(heap)) { CHECK(n<=sizeof(font_heap)); return font_heap; } return heap; }
void kfree(void *p) { (void)p; }
static u8 pixels[900*700];
static int locked, presents, fail_lock;
int gfx_console_lock(struct gfx_console_target *t, u8 **p) {
    (void)t; if (fail_lock) return -1; CHECK(!locked); locked=1; *p=pixels; return 0;
}
int gfx_console_unlock(struct gfx_console_target *t, i32 x, i32 y, i32 w, i32 h) {
    CHECK(locked && x>=0 && y>=64 && x+w<=(int)t->width && y+h<=(int)t->height);
    locked=0; return 0;
}
int gfx_console_present(struct gfx_console_target *t) { (void)t; CHECK(!locked); presents++; return 0; }
int con_sink_is_enabled(void) { return 0; }
int con_dual_active(void) { return 0; }
void con_sink_push_print(const char *s,u32 n,u8 c) { (void)s;(void)n;(void)c; }
void con_sink_push_clear(void) {}
void con_sink_push_cursor(int x,int y) { (void)x;(void)y; }
void bootlog_push(const char *s,u32 n) { (void)s;(void)n; }
int con_output_putchar(char c) { (void)c; return 0; }
#define LEGACY_FAKE_ACTIVE_ENABLE
#include "legacy_guest_fake.h"
u16 unicode_file_lookup(u32 cp) { return cp==0x3042 ? 0x2422 : 0; }
/* Byte masks are deliberately independent of the renderer's bit extraction. */
static const u8 ascii[16]={0x80,0x42,0x24,0x18,0x81,0x7e};
static const u8 kana[16]={0x20,0x40,0x80,0x02,0x01};
static const u8 jis[32]={0x80,0x01,0x42,0x24,0x18,0x81};
/* Load a complete synthetic KCG1 through the real resident provider. */
#include "drivers/video/kcg/kcg_file.h"
static u8 fixture[295956];
int font_guard_run(int (*call)(void *),void *arg) { return call(arg); }
i32 font_lease_resident_open(const char *path,u32 *id,u32 *size) {
    (void)path; *id=1; *size=sizeof(fixture); return 0;
}
i32 font_lease_resident_read_at(u32 id,u32 off,void *dst,u32 len) {
    CHECK(id==1 && off+len<=sizeof(fixture));
    for(u32 i=0;i<len;i++) ((u8 *)dst)[i]=fixture[off+i];
    return len;
}
i32 font_lease_resident_validate(u32 id) { CHECK(id==1); return 0; }
i32 font_lease_resident_close(u32 id) { CHECK(id==1); return 0; }
static void make_font(void) {
    unsigned bytes=sizeof(fixture)-16, ank_offset=16+94*94*33;
    fixture[0]='K'; fixture[1]='C'; fixture[2]='G'; fixture[3]='1';
    for(int i=0;i<4;i++) fixture[4+i]=bytes>>(i*8);
    for(int i=0;i<94*94;i++) fixture[16+94*94*32+i]=1;
    for(int i=0;i<256;i++) fixture[ank_offset+256*16+i]=1;
    for(int i=0;i<16;i++) {
        fixture[ank_offset+'A'*16+i]=ascii[i];
        fixture[ank_offset+0xb1*16+i]=kana[i];
    }
    for(int i=0;i<32;i++) fixture[16+((0x24-0x21)*94+0x22-0x21)*32+i]=jis[i];
    CHECK(!kcg_file_load("synthetic.kcg"));
}
static void cell(int x,int y,u16 expected) { u16 code=0; con_backend_readchar(x,y,&code,0); CHECK(code==expected); }
static u8 pixel(struct gfx_console_target *t,int x,int y) { return pixels[y*t->pitch+x]; }
static void mask(struct gfx_console_target *t,int x,int y,const u8 *data,int width) {
    for(int row=0;row<16;row++) for(int col=0;col<width;col++) {
        unsigned expected=(data[row*(width/8)+col/8]/(128u>>(col%8)))%2;
        CHECK(pixel(t,x+col,y+row)==(expected?15:0));
    }
}
static void scenario(unsigned w,unsigned h) {
    struct gfx_console_target t={.width=w,.height=h,.pitch=w+13};
    for(unsigned i=0;i<sizeof(pixels);i++) pixels[i]=0x55;
    CHECK(!pm_console_attach(&t,&kcg_file_api,64));
    CHECK(con_backend_safe());
    CHECK(con_backend_printf_attr(0x07)==0xe1);
    int cols=w/8, rows=(h-64)/16;
    CHECK(con_backend_geometry()->cols==cols && con_backend_geometry()->rows==rows);
    con_backend_putchar(0,0,'A',0xe1);
    CHECK(pixel(&t,0,64)==15 && pixel(&t,7,64)==0);
    CHECK(pixel(&t,1,65)==15 && pixel(&t,6,65)==15);
    con_backend_putchar(1,0,(char)0xb1,0xe1);
    CHECK(pixel(&t,10,64)==15 && pixel(&t,8,64)==0);
    mask(&t,0,64,ascii,8); mask(&t,8,64,kana,8);
    con_backend_putkanji(2,0,0x2422,0xe1);
    mask(&t,16,64,jis,16);
    cell(2,0,0x2422); cell(3,0,0x2422);
    CHECK(pixel(&t,16,64)==15 && pixel(&t,31,64)==15);
    CHECK(con_backend_reverse(3,0)==2);
    CHECK(pixel(&t,16,64)==0 && pixel(&t,17,64)==15 && pixel(&t,31,64)==0);
    CHECK(con_backend_reverse(2,0)==2);
    con_backend_cursor_at(0,0); con_backend_cursor_shape(1);
    CHECK(pixel(&t,0,78)==15);
    con_backend_cursor_at(1,1);
    CHECK(pixel(&t,0,78)==0 && pixel(&t,8,94)==15);
    con_backend_cursor_shape(0); CHECK(pixel(&t,8,94)==0);
    con_backend_putchar(3,0,' ',0xe1); cell(2,0,' '); cell(3,0,' ');
    tvram_clear(); console_set_cursor(0,0); con_backend_cursor_shape(0);
    shell_print("AA\b\rA\nA",0xe1);
    cell(0,0,'A'); cell(1,0,' '); cell(0,1,'A');
    console_set_cursor(0,2); shell_print_utf8("A\tA",0xe1); cell(4,2,'A');
    console_set_cursor(cols-1,3); shell_print("AA",0xe1);
    cell(cols-1,3,'A'); cell(0,4,'A');
    console_set_cursor(0,5); shell_print_utf8("\343\201\202",0xe1); cell(0,5,0x2422); cell(1,5,0x2422);
    tvram_set_scroll_reserve(1);
    con_backend_putchar(0,rows-1,'R',0xe1);
    con_backend_putchar(0,1,'1',0xe1); con_backend_putchar(0,2,'2',0xe1);
    con_backend_putchar(0,3,'3',0xe1);
    con_backend_putchar(0,rows-2,'Z',0xe1);
    console_set_cursor(0,rows-2); shell_print("\n\n",0xe1);
    CHECK(pixel(&t,0,64+(rows-2)*16+14)==15);
    cell(0,rows-4,'Z'); cell(0,0,'2'); cell(0,1,'3'); cell(0,rows-2,' '); cell(0,rows-1,'R');
    con_backend_cursor_shape(0);
    /* Reserved band, pitch padding and incomplete bottom scanlines stay intact. */
    for(unsigned y=0;y<h;y++) for(unsigned x=0;x<t.pitch;x++)
        if(y<64 || y>=64+(unsigned)rows*16 || x>=w) CHECK(pixel(&t,x,y)==0x55);
    CHECK(presents>0);
    tvram_set_scroll_reserve(0);
    fail_lock=1; con_backend_putchar(0,0,'A',0xe1); CHECK(!con_backend_safe()); fail_lock=0;
    pm_console_detach(); CHECK(!con_backend_safe());
}
void _start(void) { make_font(); scenario(640,480); scenario(640,400); scenario(800,600); say("PASS fbcon R1 R2\n"); finish(0); }
