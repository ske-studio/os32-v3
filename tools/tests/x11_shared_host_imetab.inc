/* IMETAB: real cooked readers and GUI KAPI, with the shared dictionary fixture. */
static int imetab_candidate(int list)
{
    reset(); user = 1; owner = 2; ime_toggle();
    key(0x1d); key(KEY_SPACE);
    if (list) key(KEY_XFER);
    while (kbd_count) CHECK(ime_trygetkey() == -1, "IMETAB composition consumed");
    CHECK(g_ime.state == (list ? IME_ST_CANDLIST : IME_ST_CONVERT) &&
          !kstrcmp(g_ime.results[0].kanji, "亜"), "IMETAB candidate ready");
    return 0;
}

static int imetab_delivery(void)
{
    int (*readers[])(void) = {ime_trygetchar, ime_trygetkey,
                             ime_getchar, ime_getkey,
                             ime_getchar_kapi, ime_getkey_kapi};
    const u8 candidate[] = "亜", kana[] = "あ";
    const u8 scans[] = {KEY_TAB, KEY_F1, 0x1d}; /* CTRL+A = 1 */
    const int chars[] = {9, 0, 1};
    for (int list = 0; list < 2; list++) {
        for (u32 r = 0; r < sizeof(readers)/sizeof(readers[0]); r++) {
            for (u32 k = 0; k < sizeof(scans); k++) {
                CHECK(!imetab_candidate(list), "IMETAB setup");
                if (k == 2) kbd_shift_state = SHIFT_CTRL;
                key(scans[k]);
                kbd_shift_state = 0;
                key(0x1d); /* queued before any candidate byte has been read */
                for (u32 b = 0; b < sizeof(candidate) - 1; b++) {
                    CHECK(readers[r]() == candidate[b], "IMETAB candidate before transparent key");
                    CHECK(kbd_count == 1, "IMETAB queued a untouched while draining");
                }
                CHECK(learns == 1, "IMETAB candidate learned once");
                if (chars[k] || (r & 1)) {
                    CHECK(readers[r]() == ((r & 1) ? (scans[k] << 8) | chars[k] : chars[k]),
                          "IMETAB original transparent key once with scan");
                    CHECK(kbd_count == 1, "IMETAB pending key before queued a");
                } else if (r == 0) {
                    CHECK(readers[r]() == -1 && kbd_count == 1,
                          "IMETAB scan-only poll consumes only pending key");
                } else {
                    /* Blocking getchar skips F1, processes queued a, then Enter. */
                    key(KEY_RETURN);
                    for (u32 b = 0; b < sizeof(kana) - 1; b++)
                        CHECK(readers[r]() == kana[b], "IMETAB scan-only wait skips pending key");
                    CHECK(ime_trygetkey() == -1 && learns == 1,
                          "IMETAB blocking tail delivered once");
                    continue;
                }
                CHECK(ime_trygetkey() == -1 && !kstrcmp(g_ime.kana_buf, "あ"),
                      "IMETAB next a becomes new preedit");
                key(KEY_RETURN);
                for (u32 b = 0; b < sizeof(kana) - 1; b++)
                    CHECK(readers[r]() == kana[b], "IMETAB next a Enter commits kana");
                CHECK(ime_trygetkey() == -1 && ime_trygetchar() == -1 && learns == 1,
                      "IMETAB empty polls do not redeliver or relearn");
            }
        }
        CHECK(!imetab_candidate(list), "IMETAB mixed setup");
        key(KEY_TAB);
        CHECK(ime_getchar() == 0xe4 && ime_trygetkey() == 0xba &&
              ime_getchar_kapi() == 0x9c && ime_getkey_kapi() == ((KEY_TAB << 8) | 9) &&
              ime_trygetchar() == -1, "IMETAB mixed readers share delivery state");

        CHECK(!imetab_candidate(list), "IMETAB GUI setup");
        CHECK(ime_feed_key((KEY_TAB << 8) | 9) == ((KEY_TAB << 8) | 9),
              "IMETAB GUI Tab remains Pass");
        for (u32 b = 0; b < sizeof(candidate) - 1; b++)
            CHECK(ime_feed_key(-1) == candidate[b], "IMETAB GUI Pass leaves collectable candidate");
        CHECK(ime_feed_key(-1) == -1 && ime_feed_key((0x1d << 8) | 'a') == -1 &&
              !kstrcmp(g_ime.kana_buf, "あ") && learns == 1,
              "IMETAB GUI drain preserves next a");
    }
    say("PASS IMETAB six CUI readers, scan/control, mixed readers, queued a and GUI boundary\n");
    return 0;
}

static int imetab_lifetime(void)
{
    static const IME_Render gui_render = {gui_draw_char, gui_draw_wide, gui_clear_row, 0, 0};
    for (int consumed = 1; consumed <= 3; consumed += 2) {
        for (int cancel = 0; cancel < 4; cancel++) {
            CHECK(!imetab_candidate(1), "IMETAB lifetime setup");
            key(KEY_TAB);
            for (int b = 0; b < consumed; b++)
                CHECK(ime_trygetkey() == (int)(u8)"亜"[b], "IMETAB lifetime partial commit");
            ime_owner_exit(3);
            if (cancel == 0) ime_owner_exit(2);
            if (cancel == 1) (void)ime_feed_key(KEY_STOP << 8);
            if (cancel == 2) ime_set_render((void *)&gui_render);
            if (cancel == 3) ime_init();
            owner = 3;
            CHECK(ime_trygetkey() == -1 && ime_trygetchar() == -1,
                  "IMETAB cancellation clears pending key and commit together");
            CHECK(learns == 1, "IMETAB cancellation never relearns");
            key(0x1d);
            if (cancel == 3) {
                CHECK(ime_trygetchar() == 'a', "IMETAB init starts fresh OFF input");
            } else {
                CHECK(ime_trygetchar() == -1 && !kstrcmp(g_ime.kana_buf, "あ"),
                      "IMETAB next reader starts fresh input");
            }
        }
    }
    /* Mode changes and an unrelated child exit are not cancellation boundaries. */
    CHECK(!imetab_candidate(0), "IMETAB mode setup");
    key(KEY_TAB);
    CHECK(ime_trygetkey() == 0xe4, "IMETAB mode partial commit");
    ime_owner_exit(3); ime_set_mode(IME_MODE_OFF);
    CHECK(ime_trygetchar() == 0xba && ime_trygetchar() == 0x9c &&
          ime_trygetkey() == ((KEY_TAB << 8) | 9) && ime_trygetkey() == -1,
          "IMETAB mode and unrelated exit preserve pending delivery");
    say("PASS IMETAB pending key cancellation and input lifetime\n");
    return 0;
}
