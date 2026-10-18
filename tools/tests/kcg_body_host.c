/* Real ROM body with deterministic I/O; proves byte/port order, not real ROM. */
#include "kcg_module.h"
#include "kcg.h"
#include "pc98.h"
#include "kstring.h"
#define IO_H
static u32 writes, waits, reads, mode_writes, logs;
static u8 code_lo, code_hi, line_sel;
static inline void outp(unsigned port, unsigned value)
{
    ++writes;
    if (port == MODE_FF1_PORT) { ++mode_writes; return; }
    if (port == KCG_CODE_LO) code_lo = value;
    if (port == KCG_CODE_HI) code_hi = value;
    if (port == KCG_LINE_SEL) line_sel = value;
}
static inline unsigned inp(unsigned port)
{
    (void)port; ++reads;
    return (code_lo * 7 + code_hi * 11 + line_sel * 13) & 255;
}
static inline void io_wait(void) { ++waits; }
#include "kcg_body_source.inc"
#include "../../kernel/kcg_module.c"
static int published;
const u32 *module_exports(struct module_handle h)
{ (void)h; static const u32 ops[2] = {1, 1}; return published ? ops : 0; }
extern char __executable_start[], etext[];
static void stop(int rc) __attribute__((noreturn));
static void stop(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void output(const char *s, u32 n)
{ __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory"); }
#define CHECK(x, why) do { if (!(x)) { output("FAIL " why "\n", sizeof("FAIL " why "\n")-1); stop(1); } } while (0)
void *kmemcpy(void *dst, const void *src, u32 n)
{ u8 *d=dst; const u8 *s=src; while(n--) *d++=*s++; return dst; }
void *kmemset(void *dst, int value, u32 n)
{ u8 *d=dst; while(n--) *d++=(u8)value; return dst; }
void kprintf(u8 a, const char *f, ...) { (void)a; (void)f; ++logs; }
void _start(void)
{
    KcgModuleApi again = {0};
    struct module_record record = {
        .text = (u32)(uptr)__executable_start,
        .text_size = (u32)(etext - __executable_start)
    };
    CHECK(!kcg_start(&record, (u32)(uptr)kcg_module_init, 0), "first typed init");
    published = 1;
    KcgModuleApi api = {kcg_rom_read_ank, kcg_rom_read_kanji, kcg_cache_stats};
    CHECK(mode_writes == 1 && waits == 1, "code mode once");
    CHECK(kcg_module_init(&again) == MODULE_BUSY && mode_writes == 1 && !again.read_ank, "double init");
    u8 glyph[32];
    api.read_ank('A', glyph);
    CHECK(reads == 16 && writes == 19 && waits == 19, "ANK port count");
    for (u32 i=0; i<16; i++) CHECK(glyph[i] == (u8)('A'*11+i*13), "ANK bytes");
    api.read_kanji(0x3021, glyph);
    CHECK(reads == 48 && writes == 53 && waits == 53, "kanji port count");
    for (u32 i=0; i<16; i++) {
        CHECK(glyph[i*2] == (u8)(0x21*7+0x10*11+(i|0x20)*13), "kanji left bytes");
        CHECK(glyph[i*2+1] == (u8)(0x21*7+0x10*11+i*13), "kanji right bytes");
    }
    for (u32 i=0; i<510; i++) { api.read_ank('A', glyph); api.read_kanji(0x3021, glyph); }
    KcgCacheStats s; api.stats(&s);
    CHECK(s.hits == 1020 && s.misses == 2 && reads == 48, "warm cache");
    api.read_kanji(0x3021, glyph); api.read_kanji(0x3021, glyph);
    CHECK(logs == 1, "one cache sample");
    /* A real valid JIS collision replaces the slot and refetches identical bytes. */
    u16 other = 0;
    for (u32 hi=0x21; hi<=0x7e && !other; hi++)
        for (u32 lo=0x21; lo<=0x7e; lo++) {
            u16 jis=(hi<<8)|lo;
            if (jis!=0x3021 && jis%KCG_CACHE_SLOTS==0x3021%KCG_CACHE_SLOTS) { other=jis; break; }
        }
    CHECK(other, "valid collision fixture");
    api.read_kanji(other, glyph); api.read_kanji(0x3021, glyph);
    CHECK(reads == 112, "eviction refetch");
    api.stats(&s); CHECK(s.hits == 1022 && s.misses == 4, "hit counters");
    output("PASS KCG body: glyphs/ports, double init, collision; sample hit=1022/1024 (99.80%)\n",
           sizeof("PASS KCG body: glyphs/ports, double init, collision; sample hit=1022/1024 (99.80%)\n")-1);
    stop(0);
}
