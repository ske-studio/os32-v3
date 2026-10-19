/* T7b-a2 test-only WM entry, part of the existing kernel-r1 image.
 * emu_write_mem may set request=1(success), 2(error), 3(wait for STOP),
 * 13(#GP), 14(#PF), 16(#MF); then let a GUI app enter the syscall pump.
 * request clears once. result/inside_cw/restored are readable after return.
 * Fault cases intentionally halt; there is no recovery from a font fault. */
#ifdef OS32_R1_FIXTURE
#include "font_guard.h"
#include "appslot.h"
#include "idt.h"
#include "io.h"
#include "memmap.h"
#include "kprintf.h"

volatile u32 font_guard_fixture_request;
volatile int font_guard_fixture_result;
volatile u32 font_guard_fixture_restored, font_guard_fixture_inside_cw;
static u8 saved[108], returned[108];

static int font_fixture_body(void *context)
{
    u32 mode = (u32)(uptr)context;
    u16 cw;
    __asm__ volatile("fnstcw %0" : "=m"(cw));
    font_guard_fixture_inside_cw = cw;
    kprintf(0x07, "[font-test] active=%u cw=%04x mode=%u\n",
            font_guard_active, (u32)cw, mode);
    if (mode == 13) __asm__ volatile("mov $0xffff, %%ax; mov %%ax, %%ds" ::: "eax");
    if (mode == 14) (void)*(volatile u32 *)P2V(0); /* actual NP page0 */
    if (mode == 16) {
        cw = 0x037e;
        __asm__ volatile("fldcw %0; fldz; fldz; fdivp; fwait" :: "m"(cw) : "st");
    }
    if (mode == 3) {
        u32 start = tick_count;
        while (!appslot_stop_pending() && tick_count - start < 500u) {}
    }
    __asm__ volatile("fld1; fldpi" ::: "st");
    return mode == 2 ? -77 : 42;
}

void font_guard_fixture(void)
{
    u32 mode = font_guard_fixture_request;
    if (!mode) return;
    font_guard_fixture_request = 0;
    font_guard_fixture_restored = font_guard_fixture_inside_cw = 0;
    if (font_x87_present)
        __asm__ volatile("fnsave %0; fwait; frstor %0" : "=m"(saved));
    font_guard_fixture_result = font_guard_run(font_fixture_body, (void *)(uptr)mode);
    if (font_x87_present) {
        __asm__ volatile("fnsave %0; fwait; frstor %0" : "=m"(returned));
        font_guard_fixture_restored = 1;
        for (u32 i = 0; i < sizeof(saved); i++) {
            if (i == 2 || i == 3 || i == 6 || i == 7 || i == 10 || i == 11 ||
                i == 26 || i == 27) continue;
            u8 mask = i == 19 ? 7 : 255;
            if ((saved[i] & mask) != (returned[i] & mask)) font_guard_fixture_restored = 0;
        }
    }
    kprintf(0x07, "[font-test] present=%u rc=%d restored=%u active=%u stop=%u\n",
            font_x87_present, font_guard_fixture_result, font_guard_fixture_restored,
            font_guard_active, (u32)appslot_stop_pending());
}
#endif
