#include "in2_int.h"
#include "drivers/input/kbd.h"
#include "input_pc98_compat_fixture.h"
static int pc98_compat_value;
volatile int kbd_gui_mode;
void legacy_cooked_push(u16 v) { pc98_compat_value = v; }
void legacy_raw_push(int sc, int br, int mods) { (void)sc; (void)br; (void)mods; }
int legacy_stop_filter(int sc, int br, int mods) { (void)sc; (void)br; (void)mods; return 0; }
void pc98_old_emit(u8 sc, u8 down, u8 mods);
static int pc98_compat_check(void)
{
    static const u8 mods[] = {0, SHIFT_SHIFT, SHIFT_CTRL, SHIFT_CTRL | SHIFT_SHIFT,
        SHIFT_GRPH, SHIFT_GRPH | SHIFT_SHIFT, SHIFT_GRPH | SHIFT_CTRL,
        SHIFT_GRPH | SHIFT_CTRL | SHIFT_SHIFT};
    /* Literal fixture column per modifier; Kana does not change cooked bytes.
       Caps swaps case before CTRL, only for alphabetic keys. */
    static const u8 plain_columns[4][8] = {
        {0, 1, 2, 3, 0, 1, 2, 3}, /* no lock */
        {0, 1, 2, 3, 0, 1, 2, 3}, /* Caps */
        {0, 1, 2, 3, 0, 1, 2, 3}, /* Kana */
        {0, 1, 2, 3, 0, 1, 2, 3}, /* Caps+Kana */
    };
    static const u8 alpha_columns[4][8] = {
        {0, 1, 2, 3, 0, 1, 2, 3}, /* no lock */
        {1, 0, 3, 2, 1, 0, 3, 2}, /* Caps */
        {0, 1, 2, 3, 0, 1, 2, 3}, /* Kana */
        {1, 0, 3, 2, 1, 0, 3, 2}, /* Caps+Kana */
    };
    for (int sc = 0; sc < 128; sc++) for (int m = 0; m < 8; m++)
    for (int locks = 0; locks < 4; locks++) {
        u8 flags = mods[m] | ((locks & 1) ? SHIFT_CAPS : 0) | ((locks & 2) ? SHIFT_KANA : 0);
        int alpha = pc98_compat_expected[sc][0] >= 'a' && pc98_compat_expected[sc][0] <= 'z';
        int column = alpha ? alpha_columns[locks][m] : plain_columns[locks][m];
        int expected = (sc >= KEY_SHIFT && sc <= KEY_CTRL) || (sc == KEY_STOP && (flags & SHIFT_CTRL)) ? -1 : (sc << 8) | pc98_compat_expected[sc][column];
        pc98_compat_value = -1; input_emit_legacy_key(sc, 1, flags);
        if (pc98_compat_value != expected) return 1;
        pc98_compat_value = -1; pc98_old_emit(sc, 1, flags);
        if (pc98_compat_value != expected) return 2;
    }
    return 0;
}
void _start(void) { int rc = pc98_compat_check(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
