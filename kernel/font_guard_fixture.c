/* T7b-a2 test-only WM entry, part of the existing kernel-r1 image.
 * emu_write_mem may set request=1(success), 2(error), 3(wait for STOP),
 * 13(#GP), 14(#PF); then let a GUI app enter the syscall pump.
 * request clears once. result is readable after return.
 * Fault cases intentionally halt; there is no recovery from a font fault.
 * TASK_FONTPERF F6: the guard no longer touches x87, so the x87 state
 * comparison and the #MF case (16) are gone. */
#ifdef OS32_R1_FIXTURE
#include "font_guard.h"
#include "appslot.h"
#include "idt.h"
#include "io.h"
#include "memmap.h"
#include "kprintf.h"

volatile u32 font_guard_fixture_request;
volatile int font_guard_fixture_result;

static int font_fixture_body(void *context)
{
    u32 mode = (u32)(uptr)context;
    kprintf(KLOG_ATTR, "[font-test] active=%u mode=%u\n", font_guard_active, mode);
    if (mode == 13) __asm__ volatile("mov $0xffff, %%ax; mov %%ax, %%ds" ::: "eax");
    if (mode == 14) (void)*(volatile u32 *)P2V(0); /* actual NP page0 */
    if (mode == 3) {
        u32 start = tick_count;
        while (!appslot_stop_pending() && tick_count - start < 500u) {}
    }
    return mode == 2 ? -77 : 42;
}

void font_guard_fixture(void)
{
    u32 mode = font_guard_fixture_request;
    if (!mode) return;
    font_guard_fixture_request = 0;
    font_guard_fixture_result = font_guard_run(font_fixture_body, (void *)(uptr)mode);
    kprintf(KLOG_ATTR, "[font-test] rc=%d active=%u stop=%u\n",
            font_guard_fixture_result, font_guard_active, (u32)appslot_stop_pending());
}
#endif
