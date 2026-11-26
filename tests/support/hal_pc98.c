#include "clock_service.h"
#include "input_service.h"
#include "pointer_service.h"
#include "sys/dev/char/rtc.h"
#include "sys/platform/pc98/input/kbd.h"
#include "sys/platform/pc98/input/mouse.h"

void pc98_d2_bind(void);
static int key = 37;
int kbd_trygetkey(void)
{
    int value = key;
    key = -1;
    return value;
}
static u8 sample(i16 *dx, i16 *dy, u8 *buttons, i16 *x, i16 *y)
{
    *dx = 9;
    *buttons = MOUSE_BTN_LEFT;
    return MOUSE_MODE_BUS;
}
const MousePointerBackend pc98_mouse_backend = { .snapshot = sample };

u8 inp(u16 port)
{
    static const u8 date[RTC_NIBBLE_COUNT] = {
        [RTC_NIB_YEAR_TENS] = 2, [RTC_NIB_YEAR_ONES] = 6, [RTC_NIB_MONTH] = 5,
        [RTC_NIB_DAY_TENS] = 1, [RTC_NIB_DAY_ONES] = 7,
    };
    static unsigned next;
    unsigned bit = next++ % RTC_READ_BITS;
    return (date[bit / 4] >> (bit % 4)) & 1;
}

void _start(void)
{
    ClockDate date;
    MouseState state = {0};
    RTC_Time legacy;
    int failed = input_trygetkey() != -1 || pointer_available() != 0;
    pc98_d2_bind();
    pointer_poll(&state);
    clock_read_date(&date);
    clock_read_legacy(&legacy);
    failed |= legacy.year != 26 || legacy.month != 5 || legacy.day != 17;
    failed |= input_trygetkey() != 37 || input_trygetkey() != -1 ||
                 state.dx != 9 || state.buttons != MOUSE_BTN_LEFT ||
                 date.year != 2026 || date.month != 5 || date.day != 17;
    __asm__ volatile("int $0x80" :: "a"(1), "b"(failed));
    __builtin_unreachable();
}
