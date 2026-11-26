#include "clock_service.h"
#include "input_service.h"
#include "pointer_service.h"
#include "input_boot.h"
#include "input_usb.h"
#include "usb_boot.h"
#include "kboot_info.h"
#include "via_window.h"
#include "sys/dev/macintosh/via.h"
#include "sys/arch/ppc/kernel_boot.h"
#include "kprintf.h"

static int key = 37;
static int ready;
volatile int kbd_gui_mode;
int pm_input_usb_take(void)
{
    int value = key;
    key = -1;
    return value;
}

int usb_input_mouse_available(void) { return 1; }
void mouse_legacy_snapshot(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{
    *dx = 9;
    *buttons = MOUSE_BTN_LEFT;
}
u32 ppc_irq_save(void) { return 0; }
void ppc_irq_restore(u32 flags) {}

const struct pm_kboot_info *ppc_boot_info(void)
{
    static const struct pm_kboot_info info = {.via.kind = PM_VIA_PMU};
    return &info;
}
const struct pm_via_window *pm_via_window(void)
{
    static const struct pm_via_window window;
    return &window;
}
int via_prepare(struct via_controller *v) { return !ready || v->kind != VIA_PMU; }
int via_read_rtc(struct via_controller *v, u32 *seconds)
{
    *seconds = 86400;
    return 0;
}
void kprintf(u8 attr, const char *format, ...) {}

void _start(void)
{
    ClockDate date;
    MouseState state = {0};
    RTC_Time legacy;
    pm_via_prepare();
    clock_read_date(&date);
    int failed = date.year != 0 || date.month != 0;
    ready = 1;
    pm_via_prepare();
    clock_read_date(&date);
    clock_read_legacy(&legacy);
    failed |= legacy.year != 4 || legacy.month != 1 || legacy.day != 2;
    failed |=  date.year != 1904 || date.month != 1 || date.day != 2;
#if CHECK_INPUT
    pm_input_boot();
    pm_usb_platform_init(1000000);
    pointer_poll(&state);
    failed |= input_trygetkey() != 37 || input_trygetkey() != -1 ||
              state.dx != 9 || state.buttons != MOUSE_BTN_LEFT;
#endif
    register int status __asm__("r3") = failed;
    register int call __asm__("r0") = 1;
    __asm__ volatile("sc" :: "r"(status), "r"(call) : "memory");
    __builtin_unreachable();
}
