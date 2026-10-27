/* Identical seam for b400acb6 and the moved grid/cursor implementation.
 * Trace every MMIO access width, value and resulting cell, complete
 * plane snapshots, logical cursor, sink/log/serial and GDC event order. */
#include "types.h"
#include "memmap.h"
#include "tvram.h"
#include "io.h"
#include "utf8.h"
#include "console_output.h"
#define PLANE_BYTES (TVRAM_CG_WINDOW - TVRAM_CHAR_BASE)
#define TRACE_WORDS 2000000
static u16 memory[(PLANE_BYTES + 4) / 2];
static u32 trace[TRACE_WORDS], count;
static int active, gui;
static void finish(int status)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(status) : "memory");
    __builtin_unreachable();
}
static void word(u32 v) { if (count == TRACE_WORDS) finish(2); trace[count++] = v; }
static void flush(void) { }
static void event(u32 kind, u32 a, u32 b) { word(kind); word(a); word(b); }
static u8 *cell(u32 address, u32 width)
{
    if (address < TVRAM_CHAR_BASE || address + width > TVRAM_CG_WINDOW) finish(3);
    return (u8 *)memory + address - TVRAM_CHAR_BASE;
}
static u32 load(u32 address, u32 width)
{
    const u8 *p=cell(address,width);u32 v=0;
    for(u32 i=0;i<width;i++) v |= (u32)p[i] << (i*8);
    event(1,address,width);word(v);return v;
}
static void store(u32 address,u32 width,u32 value)
{
    u8 *p=cell(address,width);
    for(u32 i=0;i<width;i++) p[i]=(u8)(value >> (i*8));
    event(14,address,width);word(value);
    p=cell(address & ~1u,2);word(p[0] | (u32)p[1] << 8);
}
static void port(unsigned p, unsigned v) { event(2, p, v); }
#define outp(p,v) port(p,v)
int v86_is_active(void) { return active; }
int con_sink_is_enabled(void) { return gui; }
void con_sink_enable(void) { event(3,0,0); gui=1; }
void con_sink_disable(void) { event(4,0,0); gui=0; }
void con_sink_push_clear(void) { event(5,0,0); }
void con_sink_push_cursor(int x,int y) { event(6,x,y); }
void con_sink_push_print(const char *p,u32 n,u8 c)
{ event(7,n,c); for(u32 i=0;i<n;i++) word((u8)p[i]); }
void bootlog_push(const char *p,u32 n)
{ event(8,n,0); for(u32 i=0;i<n;i++) word((u8)p[i]); }
int con_output_putchar(char c) { event(9,(u8)c,0); return 0; }
void kbd_inject_discard(void) { event(10,0,0); }
u32 kstrlen(const char *p) { u32 n=0; while(p[n]) n++; return n; }
int kutoa_dec(u32 v,char *p,int n) { (void)v;(void)n;p[0]='1';p[1]=0;return 1; }
/* Fixed conversion seam: exercise ANK, kanji and replacement widths. */
utf8_decode_t utf8_decode(const u8 *p)
{ return (utf8_decode_t){p[0] == 0xe3 ? 0x3042 : p[0], p[0] == 0xe3 ? 3 : 1}; }
u8 unicode_to_ank(u32 cp) { return cp < 0x80 ? (u8)cp : 0; }
u16 unicode_to_jis(u32 cp) { return cp == 0x3042 ? 0x2422 : 0; }
#include "console_source.inc"
#ifndef BASELINE
#include "platform/pc98/console.c"
#endif
static void snapshot(u32 label)
{
    event(11,label,0);
    word(console_get_cursor_x()); word(console_get_cursor_y());
    for(u32 i=0;i<sizeof(memory)/sizeof(memory[0]);i++) word(memory[i]);
}
static void seed(void)
{
    flush();
    for(u32 i=0;i<sizeof(memory)/sizeof(memory[0]);i++) memory[i]=(u16)(0xa500u+(i & 255));
}
void _start(void)
{
    static const int coords[][2]={{-1,0},{0,-1},{80,0},{0,25},{79,24},{78,24},{0,0},
        {0x7fffffff,0},{(-0x7fffffff-1),0},{0,0x7fffffff},{0,(-0x7fffffff-1)}};
    seed();
    for(u32 i=0;i<sizeof(coords)/sizeof(coords[0]);i++) {
        int x=coords[i][0],y=coords[i][1];u16 c=0xdead;u8 a=0xbe;
        tvram_putchar_at(x,y,(char)0xe9,0x07);
        tvram_readchar_at(x,y,&c,&a);event(12,c,a);
        tvram_readchar_at(x,y,0,0);
        tvram_readchar_at(x,y,&c,0);tvram_readchar_at(x,y,0,&a);
        tvram_putkanji_at(x,y,0x2422,0x43);
        event(13,tvram_reverse_cell(x,y),0);
        snapshot(100+i);
    }
    tvram_putkanji_at(3,2,0x2422,0x21);
    event(13,tvram_reverse_cell(4,2),0);event(13,tvram_reverse_cell(3,2),0);
    /* Orphan right half at x=0 and a wide-looking code at the right edge. */
    memory[0]=0x2284;memory[79]=0x2204;
    event(13,tvram_reverse_cell(0,0),0);event(13,tvram_reverse_cell(79,0),0);snapshot(120);
    for(int reserve=0;reserve<=24;reserve+=24) {
        seed();tvram_set_scroll_reserve(reserve);tvram_scroll();snapshot(130+reserve);
    }
    tvram_set_scroll_reserve(-1);seed();tvram_scroll();snapshot(160);
    tvram_set_scroll_reserve(99);seed();tvram_scroll();snapshot(161);
    tvram_set_scroll_reserve(0);
    for(active=0;active<2;active++) for(int mode=0;mode<2;mode++) {
        gui=0;console_set_cursor(7,8);gui=mode;seed();rshell_active=1;
        console_set_cursor(-1,99);snapshot(170);
        shell_putchar('X',0x07);shell_print("A\nB",0x43);
        shell_print_utf8("\xe3\x81\x82!\xff",0x21);
        console_write("Q\xe3\x81\x82\n",5,0x87);snapshot(171);
        tvram_clear();snapshot(172);
        console_hw_cursor_enable();console_text_gdc_stop();snapshot(173);
        console_text_gdc_start();snapshot(174);
    }
    active=0;gui=0;console_set_cursor(79,24);shell_print("xy\n",0x07);snapshot(180);
    tvram_set_scroll_reserve(24);console_set_cursor(79,0);shell_print("xy\n",0x07);snapshot(181);
    flush();
    int written;
    __asm__ volatile("int $0x80" : "=a"(written) : "a"(4), "b"(1), "c"(trace), "d"(count*sizeof(u32)) : "memory");
    finish(written != (int)(count*sizeof(u32)));
}
