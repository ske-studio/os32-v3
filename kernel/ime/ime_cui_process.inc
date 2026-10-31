/* Convert one newly fetched CUI key; readers retain wait/filter/owner policy. */
static int ime_cui_process(int keydata)
{
    /* Shift+Space is recognized both ON and OFF. */
    if (((keydata >> 8) & SCANCODE_KEY) == KEY_SPACE &&
        (kbd_shift_state & SHIFT_SHIFT)) {
        ime_toggle();
        return ime_commit_take();
    }
    if (g_ime.mode == IME_MODE_OFF) return keydata;
    return ime_delivery_process(keydata);
}
