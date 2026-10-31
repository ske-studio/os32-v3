/* Real queues exercise the per-key conversion through all six CUI APIs. */
static int cui_key_conversion(void)
{
    int (*readers[])(void) = {ime_trygetchar, ime_trygetkey,
                             ime_getchar, ime_getkey,
                             ime_getchar_kapi, ime_getkey_kapi};
    for (u32 r = 0; r < sizeof(readers) / sizeof(readers[0]); r++) {
        reset(); user = 1; owner = 2;
        kbd_shift_state = SHIFT_SHIFT;
        key(KEY_SPACE);
        if (r >= 2) key(KEY_RETURN);
        int result = readers[r]();
        CHECK(result == (r < 2 ? -1 : (r & 1) ? (KEY_RETURN << 8) | 13 : 13) &&
              g_ime.mode == IME_MODE_HIRAGANA && opens == 1,
              "IMECUI ShiftSpace turns ON through each reader");
        kbd_shift_state = 0;
        key(0x2e); /* n */
        CHECK(ime_trygetkey() == -1 && g_ime.rk.n_wait,
              "IMECUI n preedit reaches converter");
        kbd_shift_state = SHIFT_SHIFT;
        key(KEY_SPACE);
        CHECK(readers[r]() == 0xe3 && g_ime.mode == IME_MODE_OFF,
              "IMECUI ShiftSpace turns OFF and returns first unsigned byte");
        kbd_shift_state = 0;
        key(0x1d); /* a queued while the UTF-8 tail is unread */
        CHECK(readers[r]() == 0x82 && readers[r]() == 0x93 && kbd_count == 1,
              "IMECUI toggle commit tail precedes queued key");
        CHECK(readers[r]() == ((r & 1) ? (0x1d << 8) | 'a' : 'a') &&
              ime_trygetkey() == -1 && !searches && !learns,
              "IMECUI OFF keeps ASCII or original scan");
        key(KEY_F1); key(0x1d);
        if (r == 0) {
            CHECK(readers[r]() == -1 && kbd_count == 1 && readers[r]() == 'a',
                  "IMECUI poll skips only one scan-only key");
        } else if (r & 1) {
            CHECK(readers[r]() == (KEY_F1 << 8) && readers[r]() == ((0x1d << 8) | 'a'),
                  "IMECUI key reader preserves scan-only key");
        } else {
            CHECK(readers[r]() == 'a' && !kbd_count,
                  "IMECUI character wait skips scan-only key");
        }
    }
    say("PASS IMECUI six readers: ShiftSpace, n, UTF8 tail, OFF and scan-only input\n");
    return 0;
}
