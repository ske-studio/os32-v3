#include "keymap.h"
#include "in2_int.h"
#include "os32_kapi_shared.h"

static const KmBackend map = {2, 256, 0, 0, 0, 0};
int km_source_lookup(u8 src, u16 generation, KmSource *out)
{
    if (src < 1 || src > 2 || generation != 7) return OS32_ERR_INVAL;
    out->map = &map;
    out->map_generation = 1;
    return 0;
}
static int apply(u8 src, u16 code, u8 down, u8 repeat, u8 old, u8 origin, KmEffect effect)
{
    KmKey key = {src, down, repeat, 7, code};
    return km_state_apply(&key, old, origin, effect);
}
static int state(u16 mods, u16 locks)
{
    KmState s;
    km_state_snapshot(&s);
    return s.mods == mods && s.locks == locks;
}
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
int check(void)
{
    CHECK(!apply(1, 225, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_SHIFT));
    CHECK(!apply(2, 4, 1, 0, 0, INPUT_ORIGIN_USB, 0));
    CHECK(state(KM_MOD_SHIFT, 0)); /* other source evaluates A with Shift */
    CHECK(!apply(1, 229, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_SHIFT));
    CHECK(!apply(1, 225, 0, 0, 1, INPUT_ORIGIN_USB, KM_MOD_CTRL));
    CHECK(state(KM_MOD_SHIFT, 0));
    km_state_release(1, 8);
    CHECK(state(KM_MOD_SHIFT, 0));
    km_state_release(1, 7);
    CHECK(state(0, 0));
    CHECK(!apply(1, 57, 1, 0, 0, INPUT_ORIGIN_USB, KM_LOCK_CAPS));
    CHECK(!apply(1, 57, 1, 1, 1, INPUT_ORIGIN_USB, KM_LOCK_CAPS));
    CHECK(!apply(1, 57, 1, 0, 1, INPUT_ORIGIN_USB, KM_LOCK_CAPS));
    CHECK(state(0, KM_LOCK_CAPS));
    km_state_release(1, 7);
    CHECK(state(0, KM_LOCK_CAPS));
    CHECK(!apply(1, 255, 1, 0, 0, INPUT_ORIGIN_NATIVE, KM_LOCK_KANA | KM_MOD_CTRL));
    CHECK(state(KM_MOD_CTRL, KM_LOCK_CAPS | KM_LOCK_KANA));
    km_state_release(1, 7);
    CHECK(state(0, KM_LOCK_CAPS));
    CHECK(!apply(1, 1, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_SHIFT | KM_EFFECT_SHIFTA));
    CHECK(!apply(1, 1, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(state(0, KM_LOCK_CAPS | KM_LOCK_ALT));
    CHECK(!apply(1, 1, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_SHIFT | KM_EFFECT_SHIFTA));
    CHECK(!apply(2, 5, 1, 0, 0, INPUT_ORIGIN_USB, 0));
    CHECK(!apply(1, 1, 1, 1, 1, INPUT_ORIGIN_USB, KM_MOD_SHIFT | KM_EFFECT_SHIFTA));
    CHECK(!apply(1, 1, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(state(0, KM_LOCK_CAPS | KM_LOCK_ALT));
    CHECK(!apply(1, 1, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_SHIFT | KM_EFFECT_SHIFTA));
    CHECK(!apply(2, 2, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_CTRL | KM_EFFECT_SHIFTA));
    CHECK(!apply(1, 1, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(!apply(2, 2, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(state(0, KM_LOCK_CAPS | KM_LOCK_ALT)); /* intervening break cancelled second tap */
    CHECK(!apply(2, 2, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_CTRL | KM_EFFECT_SHIFTA));
    CHECK(!apply(2, 2, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(state(0, KM_LOCK_CAPS));
    CHECK(!apply(2, 2, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_CTRL | KM_EFFECT_SHIFTA));
    CHECK(!apply(2, 2, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(state(0, KM_LOCK_CAPS | KM_LOCK_ALT));
    CHECK(!apply(1, 1, 1, 0, 0, INPUT_ORIGIN_USB, KM_MOD_SHIFT | KM_EFFECT_SHIFTA));
    km_state_cancel_tap(1, 7);
    CHECK(!apply(1, 1, 0, 0, 1, INPUT_ORIGIN_USB, 0));
    CHECK(state(0, KM_LOCK_CAPS | KM_LOCK_ALT));
    CHECK(apply(1, 1, 1, 1, 0, INPUT_ORIGIN_USB, 0) == OS32_ERR_INVAL);
    CHECK(apply(1, 1, 1, 0, 0, INPUT_ORIGIN_USB, 0x8000) == OS32_ERR_INVAL);
    CHECK(apply(1, 1, 1, 0, 0, INPUT_ORIGIN_USB, KM_EFFECT_SHIFTA | KM_LOCK_CAPS) == OS32_ERR_INVAL);
    CHECK(state(0, KM_LOCK_CAPS | KM_LOCK_ALT));
    return 0;
}
void _start(void)
{
    int result = check();
    __asm__ volatile ("int $0x80" : : "a" (1), "b" (result));
    __builtin_unreachable();
}
