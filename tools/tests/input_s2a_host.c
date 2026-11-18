/* 期待の出所: INPUT_KEYMAP_DESIGN §7 S1m・S2a、K5 */
#include "keymap.h"
#include "in2_int.h"
#include "usb_input.h"
#include "os32_kapi_shared.h"
int pm_input_keymap_register(void);
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static KmKey captured;
static unsigned calls, releases;
void km_release_keys(u8 src, u16 gen) { (void)src; (void)gen; releases++; }
void km_state_release(u8 src, u16 gen) { (void)src; (void)gen; }
void input_state_release(u8 src, u16 gen) { (void)src; (void)gen; }
/* Deliberately record all handoffs: stale rejection must happen in adapter. */
int input_capture_key(const KmKey *key) { captured = *key; calls++; return 0; }
int check(void)
{
    u16 native_gen, gen, next;
    u8 kind, origin, src;
    KmSource map;
    int slots[IN2_MAX_DEV];
    u16 generations[IN2_MAX_DEV];
    unsigned count = 0;
    int native = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_NATIVE, &native_gen);
    CHECK(native > 0);
    CHECK(input_usb_register(0) == OS32_ERR_INVAL);
    CHECK(input_usb_register(7) == OS32_ERR_INVAL); /* map2 absent; rollback */
    CHECK(releases == 1); /* failed bind released its allocated source */
    CHECK(input_usb_usage(7, 4, 1, 0) == OS32_ERR_INVAL && calls == 0);
    CHECK(pm_input_keymap_register() == 0); /* no USB keyboard required */
    CHECK(pm_input_keymap_register() == OS32_ERR_INVAL);
    CHECK(input_usb_register(7) == 0);
    CHECK(input_usb_register(7) == OS32_ERR_INVAL);
    CHECK(input_usb_usage(7, 4, 1, 0) == 0);
    CHECK(calls == 1);
    src = captured.src;
    gen = captured.generation;
    CHECK(input_src_info(src, gen, &kind, &origin) && kind == INPUT_KIND_KBD && origin == INPUT_ORIGIN_USB);
    CHECK(km_source_lookup(src, gen, &map) == 0 && map.map == &hid_jp_backend);
    CHECK(captured.keycode == 4 && captured.down == 1 && captured.repeat == 0);
    CHECK(input_usb_usage(7, 4, 1, 1) == 0 && calls == 2 && captured.repeat == 1);
    CHECK(input_usb_usage(7, 4, 0, 0) == 0 && calls == 3 && captured.down == 0);
    CHECK(input_usb_register(255) == 0);
    CHECK(input_usb_usage(255, 255, 1, 0) == 0 && captured.src != src && captured.keycode == 255);
    input_usb_release(255);
    unsigned before = releases;
    input_usb_release(7);
    CHECK(releases == before + 1 && !input_src_info(src, gen, &kind, &origin));
    CHECK(km_source_lookup(src, gen, &map) == OS32_ERR_INVAL);
    CHECK(input_usb_usage(7, 4, 1, 0) == OS32_ERR_INVAL && calls == 4);
    input_usb_release(7);
    CHECK(releases == before + 1);
    CHECK(input_usb_register(7) == 0);
    CHECK(input_usb_usage(7, 4, 1, 0) == 0);
    src = captured.src;
    gen = captured.generation;
    while (count < IN2_MAX_DEV) {
        int slot = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &next);
        if (slot < 0) break;
        slots[count] = slot;
        generations[count++] = next;
    }
    input_src_release(src, gen); /* external release/reuse: stored mapping is stale */
    CHECK(input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &next) == src && next == gen + 1);
    CHECK(input_usb_usage(7, 4, 1, 0) == OS32_ERR_INVAL && calls == 5);
    before = releases;
    input_usb_release(7);
    CHECK(releases == before && input_src_info(src, next, &kind, &origin));
    input_src_release(src, next);
    while (count) {
        --count;
        input_src_release(slots[count], generations[count]);
    }
    CHECK(input_src_info(native, native_gen, &kind, &origin));
    /* Pool exhaustion is propagated, and failed USB registration is retryable. */
    for (unsigned i = 1; i < IN2_MAX_DEV; ++i) {
        slots[count] = input_src_register(INPUT_KIND_KBD, INPUT_ORIGIN_USB, &generations[count]);
        CHECK(slots[count] > 0);
        ++count;
    }
    CHECK(input_usb_register(8) == OS32_ERR_FULL);
    --count;
    input_src_release(slots[count], generations[count]);
    CHECK(input_usb_register(8) == 0);
    input_usb_release(8);
    return 0;
}
void _start(void) { int rc = check(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
