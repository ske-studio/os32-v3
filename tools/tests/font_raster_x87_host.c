/* T7b-a5 host fixture: run the OS32-target raster (x87) inside the product
 * font_guard_run (kernel/font_guard.c + kernel/font_x87.asm, CW 037F).
 * Files in cwd: font.bin, cps.bin (u32 LE) -> out.bin. Linux i386 syscalls. */
#include "font_guard.h"
volatile u32 kctx_irq_depth, kctx_exc_depth;
struct probe { const u8 *font; u32 font_len; const u32 *cps; u32 count; u8 *out; u32 cap; u32 len; };
extern int font_raster_probe(void *context);
static u8 font[8u << 20];   /* ipaexg.ttf is 5.8 MiB */
static u32 cps[16384];
static u8 out[16u << 20];
static int sys(int n, int a, int b, int c) {
    int r;
    __asm__ volatile("int $0x80" : "=a"(r) : "a"(n), "b"(a), "c"(b), "d"(c) : "memory");
    return r;
}
static void die(int rc) __attribute__((noreturn));
static void die(int rc) { sys(1, rc, 0, 0); for (;;) {} }
static u32 slurp(const char *name, void *dst, u32 cap) {
    int fd = sys(5, (int)name, 0, 0), n;
    u32 got = 0;
    if (fd < 0) die(2);
    while ((n = sys(3, fd, (int)((u8 *)dst + got), (int)(cap - got))) > 0) got += (u32)n;
    if (n < 0 || got == cap) die(3);
    sys(6, fd, 0, 0);
    return got;
}
void _start(void) {
    static struct probe p;
    u16 cw = 0;
    p.font = font; p.font_len = slurp("font.bin", font, sizeof font);
    p.cps = cps; p.count = slurp("cps.bin", cps, sizeof cps) / 4;
    p.out = out; p.cap = sizeof out;
    font_x87_present = 1;
    __asm__ volatile("fninit; fldcw %0" :: "m"((u16){0x0f7f}));  /* caller: other CW */
    int rc = font_guard_run(font_raster_probe, &p);
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    if (cw != 0x0f7f) die(4);                                      /* guard restored */
    if (rc != 0) die(rc >= 1000 ? 10 + (rc - 1000) : 5);
    int fd = sys(5, (int)"out.bin", 0x241, 0644);
    if (fd < 0) die(6);
    for (u32 at = 0; at < p.len;) { int n = sys(4, fd, (int)(out + at), (int)(p.len - at)); if (n <= 0) die(7); at += (u32)n; }
    sys(6, fd, 0, 0);
    die(0);
}
