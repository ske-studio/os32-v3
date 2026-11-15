#include "in2_int.h"
#include "io.h"
#include "kbd.h"
#include "legacy_guest.h"

static u8 cnt[128], mechanical[IN2_MAX_DEV], usb_lock;

int input_key_pressed(u8 sc)
{
    return sc < 128 && cnt[sc] != 0;
}

u8 input_key_mods(void)
{
    unsigned int flags = irq_save();
    u8 mods = usb_lock;
    for (unsigned int i = 0; i < IN2_MAX_DEV; ++i) mods |= mechanical[i];
    if (cnt[KEY_SHIFT]) mods |= SHIFT_SHIFT;
    if (cnt[KEY_CTRL]) mods |= SHIFT_CTRL;
    if (cnt[KEY_GRPH]) mods |= SHIFT_GRPH;
    irq_restore(flags);
    return mods;
}

void input_merge_key(InputEvent *e, u8 old, u8 origin)
{
    u8 sc = e->sc, down = e->down;
    if (old != down) {
        cnt[sc] += down ? 1 : -1;
        u8 bit = sc == KEY_CAPS ? SHIFT_CAPS : sc == KEY_KANA ? SHIFT_KANA : 0;
        if (origin == INPUT_ORIGIN_USB) {
            if (down) usb_lock ^= bit;
        } else if (down) mechanical[e->src - 1] |= bit;
        else mechanical[e->src - 1] &= ~bit;
        e->buttons = input_key_mods();
        if (legacy_active()) return;
        if (input_emit_in2) input_emit_in2(e);
        if (down || cnt[sc] == 0) {
            if (input_emit_legacy_key) input_emit_legacy_key(sc, down, e->buttons);
        }
    } else if (down && !legacy_active()) {
        if (input_emit_legacy_repeat) input_emit_legacy_repeat(sc, input_key_mods());
    }
}
