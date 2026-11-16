#include "keymap.h"
#include "in2_int.h"
#include "os32_kapi_shared.h"

#define KM_KEY_COUNT 256
#define MOD_MASK (KM_MOD_SHIFT | KM_MOD_CTRL | KM_MOD_ALT | KM_MOD_MODE | KM_MOD_COMMAND | KM_MOD_META)
#define LOCK_MASK (KM_LOCK_CAPS | KM_LOCK_NUM | KM_LOCK_KANA | KM_LOCK_SCROLL | KM_LOCK_ALT)
#define EFFECT_MASK (MOD_MASK | LOCK_MASK | KM_EFFECT_SHIFTA)

static struct {
    KmEffect effect[KM_KEY_COUNT];
    u16 generation;
    u8 active, origin;
} saved[IN2_MAX_DEV];
static u16 shared_locks;
static struct {
    u16 generation, keycode;
    u8 src, armed;
} tap;

static int valid_effect(KmEffect effect)
{
    u16 mods = effect & MOD_MASK;
    if (effect & ~EFFECT_MASK) return 0;
    if (effect & KM_EFFECT_SHIFTA) {
        if ((effect & LOCK_MASK) ||
            (mods != KM_MOD_SHIFT && mods != KM_MOD_CTRL && mods != KM_MOD_ALT))
            return 0;
    }
    return 1;
}

int km_state_apply(const KmKey *key, u8 old_down, u8 origin, KmEffect effect)
{
    KmSource source;
    unsigned int index;
    KmEffect previous;
    int same_tap;
    if (!key || key->src < 1 || key->src > IN2_MAX_DEV ||
        key->down > 1 || key->repeat > 1 || old_down > 1 ||
        (key->repeat && (!key->down || !old_down)) ||
        (origin != INPUT_ORIGIN_NATIVE && origin != INPUT_ORIGIN_USB) ||
        !valid_effect(effect) || km_source_lookup(key->src, key->generation, &source) ||
        key->keycode >= source.map->count || key->keycode >= KM_KEY_COUNT)
        return OS32_ERR_INVAL;
    index = key->src - 1;
    if (saved[index].active && (saved[index].generation != key->generation ||
                                saved[index].origin != origin))
        return OS32_ERR_INVAL;
    saved[index].active = 1;
    saved[index].generation = key->generation;
    saved[index].origin = origin;
    previous = saved[index].effect[key->keycode];
    same_tap = tap.armed && tap.src == key->src &&
               tap.generation == key->generation && tap.keycode == key->keycode;
    if (tap.armed && !same_tap && (key->down || old_down)) tap.armed = 0;
    if (key->down && !old_down) {
        saved[index].effect[key->keycode] = effect;
        if (origin == INPUT_ORIGIN_USB) shared_locks ^= effect & LOCK_MASK;
        if (effect & KM_EFFECT_SHIFTA) {
            tap.src = key->src;
            tap.generation = key->generation;
            tap.keycode = key->keycode;
            tap.armed = 1;
        }
    } else if (!key->down && old_down) {
        if (same_tap && (previous & KM_EFFECT_SHIFTA)) shared_locks ^= KM_LOCK_ALT;
        if (same_tap) tap.armed = 0;
        saved[index].effect[key->keycode] = 0;
    }
    return 0;
}

void km_state_snapshot(KmState *out)
{
    u16 mods = 0, locks = shared_locks;
    if (!out) return;
    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) {
        if (!saved[i].active) continue;
        for (unsigned int k = 0; k < KM_KEY_COUNT; ++k) {
            KmEffect effect = saved[i].effect[k];
            mods |= effect & MOD_MASK;
            if (saved[i].origin == INPUT_ORIGIN_NATIVE) locks |= effect & LOCK_MASK;
        }
    }
    out->mods = mods;
    out->locks = locks;
}

void km_state_cancel_tap(u8 src, u16 generation)
{
    if (tap.armed && tap.src == src && tap.generation == generation) tap.armed = 0;
}

void km_state_release(u8 src, u16 generation)
{
    if (src < 1 || src > IN2_MAX_DEV || !saved[src - 1].active ||
        saved[src - 1].generation != generation) return;
    km_state_cancel_tap(src, generation);
    for (unsigned int k = 0; k < KM_KEY_COUNT; ++k) saved[src - 1].effect[k] = 0;
    saved[src - 1].active = 0;
}
