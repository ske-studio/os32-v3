/* Real fixed diagnostic; mock ROM/timing only, no claim about real A9h timing. */
#include <stdarg.h>
#include "kcg_module.h"
#include "kcg.h"
#include "pc98.h"
#include "paging.h"
#include "gfx/gfx.h"

static u8 code_lo, code_hi, line_sel, mode = MFF1_KCG_CODE;
static u32 misses, samples;
static int present;
/* Independent ANK fixture: blank margins, strokes across the middle of A. */
static const u8 rom_a[KCG_ANK_H] = {
    0, 0, 0x10, 0x28, 0x44, 0x44, 0x44, 0x7c,
    0x44, 0x44, 0x44, 0x44, 0, 0, 0, 0
};
static void stop(int rc) __attribute__((noreturn));
static void stop(int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
static void check(int ok, const char *why)
{
    if (!ok) {
        u32 n = 0;
        while (why[n]) n++;
        __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(why), "d"(n) : "memory");
        stop(1);
    }
}
u32 paging_pte_flags(u32 addr)
{ return PTE_PRESENT | (addr == TVRAM_CG_WINDOW ? 0 : PTE_PCD); }
void kcg_rom_read_ank(u8 ch, u8 *buf)
{
    check(ch == 'A', "FAIL cache glyph\n");
    if (present) misses++;
    for (u32 i = 0; i < KCG_ANK_H; i++) buf[i] = present ? rom_a[i] : 0;
}
void kcg_cache_stats(KcgCacheStats *out)
{ *out = (KcgCacheStats){ .misses = misses }; }
static void outp(u32 port, u32 value)
{
    if (port == KCG_CODE_LO) code_lo = value;
    if (port == KCG_CODE_HI) code_hi = value;
    if (port == KCG_LINE_SEL) line_sel = value;
    if (port == MODE_FF1_PORT) mode = value;
}
static void io_wait(void) { }
static u32 inp(u32 port)
{
    check(port == KCG_DATA && code_lo == 0 && code_hi == 'A' &&
          line_sel < KCG_ANK_H, "FAIL fixed ROM address\n");
    /* CODE outside VSYNC is modelled as zero; dot can read the ROM. */
    return mode == MFF1_KCG_BITMAP ? rom_a[line_sel] : 0;
}
void v86_io_reset_policy(void) { mode = MFF1_KCG_CODE; }
void kprintf(u8 attr, const char *fmt, ...)
{
    (void)attr;
    if (fmt[10] != 'k') return; /* Only the kcg/A9 diagnostic line. */
    va_list ap;
    va_start(ap, fmt);
    u32 ready = va_arg(ap, u32), code = va_arg(ap, u32);
    u32 dot = va_arg(ap, u32), restored = va_arg(ap, u32);
    check(ready == (u32)present, "FAIL ready\n");
    check(rom_a[line_sel] != 0 && code != dot && restored == code,
          "FAIL diagnostic blank or indistinguishable\n");
    va_end(ap);
    samples++;
}
#include "kcg_display_source.inc"
void _start(void)
{
    for (present = 0; present < 2; present++) test_kcg_display_return();
    check(samples == 2, "FAIL diagnostic samples\n");
    stop(0);
}
