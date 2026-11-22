/* Freestanding AC-INPUT-1 vectors over the production registry, HAL and provider. */
#include "input_usb.h"
#include "kbd.h"
#include "keymap.h"
#include "usb_input.h"
#include "usb_platform.h"
#include "in2_export.h"
#include "kbd_contract.h"
#include "io.h"

#include "input_boot.h"
#include "input_service.h"
#include "input/hal/in2_int.h"
static int cooked(int sc, int c) { return (sc << 8) | c; }
static const InputOps *ops;
static int lan[256], ln, lp, polls;
unsigned int host_arch_if = 0x202;
static u32 now;
int con_sink_capture_active(void) { return 0; }
static int aborted, inject = -1, wait_phase;
volatile u32 tick_count;
void ring3_abort_request(void) { aborted = 1; }
int ring3_wait_pending(void) { return aborted; }
int legacy_active(void) { return 0; }
int in2_excl_keyboard(void) { return 0; }
u32 in2_time_ms(void) { return now; }
void usbp_log(const char *fmt, ...) { (void)fmt; }
void appslot_poll_yield_reset(void) {}
int kbd_inject_peek(u8 *b) { if (inject < 0) return 0; *b = inject; return 1; }
int kbd_inject_take(u8 *b) { int ok = kbd_inject_peek(b); inject = -1; return ok; }

static int pending_lan = -1;
void idle_work_run(void) { if (pending_lan >= 0) { lan[ln++] = pending_lan; pending_lan = -1; } }
void arch_cpu_idle(void) {}
void input_register(const InputOps *p) { ops = p; }
void pm_terminal_poll(void) { if (!_irq_enabled()) __builtin_trap(); polls++; }
int pm_terminal_trygetkey(void) { return lp < ln ? lan[lp++] : -1; }
int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = now; *hi = 0; return 0; }
static void write_text(const char *s)
{
    unsigned n = 0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}
static void check(int ok, int code) { if (!ok) { write_text("FAIL\n"); finish(code); } }
static void tap(u8 src, u8 usage) { usbp_input_usage(src, usage, 1); usbp_input_usage(src, usage, 0); }
static void empty(void) { check(pm_input_usb_take() == -1, 11); }
static int terminal_on(void) { return 1; }
/* Apply the same make/repeat/break sequence to native sc and USB usage. */
static void compare_keyboards(int usb, u8 native, u16 generation)
{
    const u8 scans[] = {KEY_SHIFT, 0x1d, 0x1d, 0x1d, KEY_SHIFT};
    const u8 usages[] = {0xe1, 4, 4, 4, 0xe1};
    const int downs[] = {1, 1, 1, 0, 0};
    for (int gui = 0; gui <= 1; ++gui) {
        kbd_set_gui_mode(gui);
        in2_ring_clear();
        for (unsigned i = 0; i < sizeof(scans); ++i) {
            KmKey k = { .src = native, .generation = generation, .keycode = scans[i],
                        .down = downs[i], .repeat = i == 2 };
            if (!usb) check(!input_capture_key(&k), 17);
            else if (i == 2) usbp_input_usage_repeat(1, usages[i]);
            else usbp_input_usage(1, usages[i], downs[i]);
            check(input_key_pressed(scans[i]) == downs[i], 17);
            u8 mods = i < 4 ? SHIFT_SHIFT : 0;
            check(input_key_mods() == mods, 17);
            InputEvent e;
            u16 seq;
            int got = in2_ring_pop(&e, &seq);
            check(i == 2 ? !got : got && e.sc == scans[i] && e.down == downs[i], 17);
            if (gui)
                check(legacy_raw_take() == (scans[i] | (downs[i] << 8) |
                                            (mods << KBD_RAW_MOD_SHIFT)), 17);
            check(pm_input_usb_take() == (!gui && (i == 1 || i == 2) ? 0x1d41 : -1), 17);
        }
    }
    kbd_set_gui_mode(0);
}

static void test(void)
{
    write_text("START\n");
    check(usbp_input_src_alloc(USBP_INPUT_KBD) < 0 && !input_src_enumerate(0, 0), 10);
    check(usb_input_alloc(USBP_INPUT_KBD) < 0 && !input_src_enumerate(0, 0), 10);
    int mouse = usbp_input_src_alloc(USBP_INPUT_MOUSE);
    check(mouse == 1 && input_src_enumerate(0, 0) == 1, 10);
    pm_input_keymap_boot();
    for (int i = 1; i <= 4; ++i) check(usbp_input_src_alloc(USBP_INPUT_KBD) == i, 10);
    check(usbp_input_src_alloc(USBP_INPUT_KBD) < 0 && input_src_enumerate(0, 0) == 5, 10);
    int second = usbp_input_src_alloc(USBP_INPUT_MOUSE);
    IN2_DevInfo info;
    check(!input_src_enumerate(1, &info) && info.kind == INPUT_KIND_KBD, 10);
    u16 gen = 1;
    u8 kind, origin;
    while (gen && !input_src_info(info.dev, gen, &kind, &origin)) ++gen;
    check(gen && origin == INPUT_ORIGIN_USB, 10);
    usbp_input_usage(1, 0xe1, 1);
    usbp_input_usage(2, 0xe5, 1);
    usbp_input_release(USBP_INPUT_KBD, 1);
    check(input_key_pressed(KEY_SHIFT) && usbp_input_src_alloc(USBP_INPUT_KBD) == 1, 10);
    check(input_src_info(info.dev, (u16)(gen + 1), &kind, &origin), 10);
    KmKey stale = { .src = info.dev, .generation = gen, .keycode = 4, .down = 1 };
    in2_ring_clear();
    InputEvent event;
    u16 seq;
    check(input_capture_key(&stale) < 0 && !input_key_pressed(0x1d) &&
          !in2_ring_pop(&event, &seq) && pm_input_usb_peek() < 0, 10);
    usbp_input_mouse(mouse, -11, 8, 7);
    i16 dx, dy, x, y;
    u8 buttons;
    mouse_legacy_snapshot(&dx, &dy, &buttons, &x, &y);
    check(dx == -11 && dy == 8 && buttons == 7, 10);
    mouse_legacy_snapshot(&dx, &dy, &buttons, &x, &y);
    check(!dx && !dy, 10);
    check(in2_ring_pop(&event, &seq) && event.type == INPUT_EVENT_MOVE &&
          event.x == -11 && event.y == 8, 10);
    check(in2_ring_pop(&event, &seq) && event.type == INPUT_EVENT_BUTTON &&
          event.buttons == 7 && !in2_ring_pop(&event, &seq), 10);
    usbp_input_mouse(second, 0, 0, 2);
    usbp_input_mouse(mouse, 0, 0, 5);
    check(input_buttons() == 7, 10);
    usbp_input_mouse(mouse, 0, 0, 4);
    check(input_buttons() == 6, 10);
    usbp_input_release(USBP_INPUT_MOUSE, mouse);
    check(input_buttons() == 2, 10);
    usbp_input_mouse(second, -1, 2, 0);
    usbp_input_release(USBP_INPUT_MOUSE, second);
    mouse_legacy_snapshot(&dx, &dy, &buttons, &x, &y);
    check(!buttons && !dx && !dy, 10);
    write_text("MAP\n");
    tap(1,4);
    usbp_input_usage(2,0xe5,0);
    check(pm_input_usb_has_key() && pm_input_usb_has_key(),12);
    /* P3 ③b-1: peek returns the oldest key repeatedly and leaves it queued. */
    check(pm_input_usb_peek()==cooked(0x1d, 'A') && pm_input_usb_peek()==cooked(0x1d, 'A'),12);
    check(pm_input_usb_take()==cooked(0x1d, 'A'),12);
    check(pm_input_usb_peek()==-1,12);
    empty();
    check(platform_fep_key_local() && platform_fep_key_shift()==SHIFT_SHIFT,12);
    u16 native_gen;
    int native = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, &native_gen);
    check(native > 0 && !km_backend_register(&pc98_jp_backend) &&
          !km_source_bind(native, native_gen, KM_MAP_PC98), 17);
    compare_keyboards(0, native, native_gen);
    compare_keyboards(1, native, native_gen);
    input_src_release(native, native_gen);
    in2_ring_clear();
    usbp_input_usage(1, 4, 1);
    check(in2_ring_pop(&event, &seq) && event.down, 13);
    usbp_input_usage_repeat(1, 4);
    check(!in2_ring_pop(&event, &seq), 13);
    usbp_input_usage(1, 4, 0);
    check(pm_input_usb_take()==0x1d61 && pm_input_usb_take()==0x1d61,13);
    empty();
    usbp_input_usage(1,0xe0,1); tap(2,6);
    check(pm_input_usb_take()==0x2b03 && platform_fep_key_shift()==SHIFT_CTRL,13);
    usbp_input_usage(1,0xe0,0);
    write_text("STATE\n");
    usbp_input_usage(1,0xe1,1);
    for (unsigned i=0;i<KBD_BUF_SIZE;i++) tap(2, i&1 ? 5 : 4);
    usbp_input_usage(1,0xe1,0);
    tap(2,4); check(pm_input_usb_dropped()==1,14);
    for (unsigned i=0;i<KBD_BUF_SIZE;i++)
        check(pm_input_usb_take()==(i&1?0x2d42:0x1d41) && platform_fep_key_shift()==SHIFT_SHIFT,14);
    tap(1,4); legacy_cooked_flush(); tap(1,4);
    check(pm_input_usb_take()==0x1d61 && !platform_fep_key_shift(),14);
    write_text("FIFO\n");
    tap(1,4); pm_input_boot(); /* Registration retains queued input. */
    usbp_input_usage(2,0xe1,1); tap(2,0x2c); usbp_input_release(USBP_INPUT_KBD,2);
    check(ops->trygetkey()==cooked(0x1d, 'a') && ops->trygetkey()==cooked(0x34, ' '),15);
    tap(1,4);
    lan[0]='x'; lan[1]='y'; ln=2;
    int before=polls;
    /* 期待の出所: PPC_P3_DESIGN 末尾の lan-first の PM 決定。 */
    check(ops->peekkey()=='x' && ops->peekkey()=='x' && polls==before,15);
    check(platform_fep_key_local() && ops->shift_state()==SHIFT_SHIFT,15);
    check(ops->trygetkey()=='x',15);
    check(!platform_fep_key_local() && !platform_fep_key_shift() && !ops->shift_state(),15);
    tap(1,5); check(ops->trygetchar()=='y' && ops->trygetkey()==cooked(0x1d, 'a') && ops->getkey()==cooked(0x2d, 'b'),15);
    tap(1,0x52); check(ops->getchar()==0x1e,15);
    check(ops->dropped_count()==1 && !ops->modifiers() && !ops->shift_state() && !ops->is_pressed(0x70),15);
    ln=lp=0; lan[ln++]=27; check(ops->trygetkey()==-1,15);
    tap(1,4); check(ops->trygetkey()==cooked(0x1d, 'a'),15);
    now=50000; check(ops->getkey()==27,15);
    ln=lp=0;
    for(unsigned i=0;i<64;i++) lan[ln++]=i&1?'\n':'\r';
    /* 期待の出所: PPC_P3_DESIGN 末尾の lan-first の PM 決定。 */
    tap(1,4);
    for(unsigned i=0;i<32;i++) check(ops->getkey()==13,15);
    check(ops->peekkey()==cooked(0x1d, 'a') && ops->peekkey()==cooked(0x1d, 'a') && pm_input_usb_peek()==cooked(0x1d, 'a'),15);
    check(!platform_fep_key_local(),15);
    check(ops->getkey()==cooked(0x1d, 'a'),15);
    tap(1,5); check(ops->getkey()==cooked(0x2d, 'b'),15);
    /* A waiting reader advances LAN via idle work, never via peek's pump. */
    pending_lan='z'; check(ops->getkey()=='z' && polls==before,15);
    /* Opus N1: a busy peek loop advances LAN through idle work (no getkey/halt). */
    pending_lan='q'; check(ops->peekkey()=='q' && ops->trygetkey()=='q' && polls==before,15);
    const struct InputLegacyProvider *saved = input_legacy_provider();
    struct InputLegacyProvider probe = *saved;
    probe.try_key = pm_input_usb_take;
    probe.terminal_try = pm_terminal_trygetkey;
    probe.terminal_enabled = terminal_on;
    input_legacy_register(&probe);
    lan[ln++]='t'; tap(1,4);
    check(ops->trygetchar_local()=='a' && lp < ln,15);
    check(ops->trygetkey()=='t',15);
    input_legacy_register(saved);
    write_text("WAIT\n");
    wait_phase = 1;
    check(ops->getkey_wait(1) < 0 && aborted && pm_input_usb_peek() < 0, 16);
    aborted = 0;
    usbp_input_usage(1, 0x48, 0);
    usbp_input_usage(1, 0xe0, 0);
    ops->gui_mode(1);
    tap(1, 4);
    check((ops->trygetrawkey() & 0x1ff) == 0x11d && ops->trygetrawkey() == 0x1d, 16);
    inject = '!';
    check(ops->peekkey() == '!' && ops->peekkey() == '!' &&
          ops->trygetkey() == '!' && ops->trygetkey() < 0, 16);
    ops->discard_stop();
    wait_phase = 1;
    check(ops->getkey_wait(1) == 'w' && !aborted, 16);
    pm_input_keymap_boot(); /* Duplicate registration fails while the map still exists. */
    int live = input_src_enumerate(0, 0);
    check(usbp_input_src_alloc(USBP_INPUT_KBD) < 0 && input_src_enumerate(0, 0) == live, 16);
    check(usbp_input_src_alloc(USBP_INPUT_MOUSE) > 0, 16);
    write_text("PROVIDER\n");
}
void _start(void) { test(); finish(0); }

void idle_wait_step(void)
{
    if (wait_phase == 1) {
        usbp_input_usage(1, 0xe0, 1);
        usbp_input_usage(1, 0x48, 1);
        wait_phase = 2;
    } else if (wait_phase == 2 && kbd_gui_mode) {
        inject = 'w';
        wait_phase = 0;
    }
    idle_work_run();
}
