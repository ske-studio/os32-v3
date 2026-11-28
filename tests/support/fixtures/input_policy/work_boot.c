/* At the first possible hardware scan, submit an already-due repeat.
 * Registration order is observed through the legacy FIFO. */
#include "keymap.h"
#include "io_work.h"
#include "io_policy.h"
#include "os32/os32_kapi_shared.h"
#include "usb_input.h"
#include "in2_int.h"
#include "usb_platform.h"
#include "sys/dev/usb/hid_kbd.c"
#ifdef __PPC__
#include "sys/platform/powermac/input_keymap_backend.c"
void pm_usb_platform_init(u32 hz) { (void)hz; }
void usbp_log(const char *fmt, ...) { (void)fmt; }
#else
#include "sys/platform/pc98/boot.c"
void tvram_print(int x, int y, const char *s, u8 color)
{ (void)x; (void)y; (void)s; (void)color; }
void kprintf(u8 color, const char *s, ...) { (void)color; (void)s; }
void mouse_init(void) {}
#endif
unsigned input_test_irq;
volatile u32 tick_count;
static int early;
static void first_device(void)
{
    int handle = usb_input_alloc(USBP_INPUT_KBD);
    if (handle < 0) { early = -2; return; }
    struct usb_iface iface = {.src = handle};
    struct hid_kbd keyboard = {.boot.iface = &iface, .repeat = 0xffff};
    iface.sc = &keyboard;
    u8 bytes[8] = {0, 0, 4};
    struct usb_xfer x = {.actual = sizeof bytes, .ctx = &keyboard};
    x.buf.va = bytes;
    kbd_done(&x);
    if ((legacy_cooked_take() & 255) != 'a') { early = -2; return; }
    kbd_tick(0, &iface, 500);
    early = legacy_cooked_take();
}
#ifdef __PPC__
void pm_usb_scan(void) { first_device(); }
#else
void kbd_init(void) { km_backend_register(&hid_jp_backend); first_device(); }
#endif
int legacy_active(void) { return 0; }
u32 usbp_ms(void) { return 0; }
int usb_xfer_start(struct usb_xfer *x) { (void)x; return 0; }
u32 in2_time_ms(void) { return 0; }
void input_emit_in2(const InputEvent *e) { (void)e; }
void input_state_release(u8 src, u16 gen) { (void)src; (void)gen; }
void ring3_abort_request(void) {}
int in2_excl_keyboard(void) { return 0; }
int in2_excl_drop_raw(u8 sc, int up) { (void)sc; (void)up; return 0; }
static int dummy(void *ctx, struct io_slice *slice) { (void)ctx; (void)slice; return IO_DONE; }
#ifdef FULL
int __real_io_register(const struct io_desc *);
int __wrap_io_register(const struct io_desc *desc)
{
    return desc->id == IO_SVC_INPUT_REPEAT ? OS32_ERR_FULL : __real_io_register(desc);
}
#endif
int probe(void)
{
#ifdef OCCUPIED
    const struct io_desc occupied = {.id = IO_SVC_INPUT_REPEAT, .default_tier = 2,
        .default_budget_us = 120, .budget_min_us = 120, .budget_max_us = 120,
        .max_unit_us = 30, .step = dummy};
    if (io_register(&occupied)) return 1;
#endif
#ifdef __PPC__
    pm_usb_boot(25000000);
#else
    pc98_boot_input(0);
#endif
#ifdef CHECK_REGISTRATION
    /* Ask the dispatcher itself; an absent service is a behavioural RED. */
    return io_unregister(0x49525054u) == 0 ? 0 : 4;
#elif defined(OCCUPIED) || defined(FULL)
    if (early < 0 || (early & 255) != 'a') return 3;
#else
    if (early != -1) return 2;
#endif
    return 0;
}
