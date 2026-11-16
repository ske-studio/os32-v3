/* Included by the real IME/keyboard fixture; device and dictionary seams only. */
static int input_m7_3_test(void)
{
    KbdPolicyImeSnapshot state;
    KbdPolicySnapshot current;
    _enable();
    user = 1; sink = kbd_gui_mode = rshell_active = 0;
    abort_requested = stop_requested = 0;
    ime_init();
    g_ime.render = &audit_ime_render;
    CHECK(!input_context_owner_register(2, 8, 0, 0), "M7 register");
    CHECK(!input_context_owner_select(2, 8), "M7 select");
    g_ime.dict_loaded = 1;
    ime_toggle();
    input_ime_snapshot(&state);
    CHECK(!state.active && ime_is_active(), "M7 empty ON inactive");
    CHECK(ime_feed_key('k') == -1, "M7 roman input");
    input_ime_snapshot(&state);
    CHECK(state.active && state.owner == 2 && state.exec_gen == 8,
          "M7 composition published");
    CHECK(!input_ime_cancel_capture(&state), "M7 capture");
    _disable();
    CHECK(ime_feed_key(-1) == -1 && g_ime.rk.preedit[0], "M7 IF0 preserves pending");
    _enable();
    CHECK(ime_feed_key(-1) == -1 && !g_ime.rk.preedit[0], "M7 pending consumed");
    input_ime_snapshot(&state);
    CHECK(!state.active && ime_is_active() && !abort_checks, "M7 cancel keeps mode");
    ime_feed_key('k');
    CHECK(!input_context_snapshot(&current) && input_ime_cancel_take(&current) == -1,
          "M7 consumed once");
    input_ime_snapshot(&state);
    CHECK(state.active, "M7 next composition");
    state.exec_gen++;
    CHECK(!input_ime_cancel_capture(&state), "M7 stale capture");
    ime_feed_key(-1);
    CHECK(g_ime.rk.preedit[0], "M7 stale generation ignored");
    CHECK(ime_feed_key(KEY_STOP << 8) == -1 && !g_ime.rk.preedit[0], "M7 legacy STOP");
    ime_feed_key('k'); ime_feed_key('a');
    input_ime_snapshot(&state);
    CHECK(state.active && g_ime.kana_len, "M7 kana published");
    CHECK(ime_feed_key(KEY_RETURN << 8) == 0xe3 && ime_feed_key(-1) == 0x81 &&
          ime_feed_key(-1) == 0x8b, "M7 UTF8 commit");
    input_ime_snapshot(&state);
    CHECK(!state.active, "M7 commit ends composition");
    /* Candidate navigation retains active; cancellation does not learn. */
    g_ime.state = IME_ST_CONVERT; g_ime.converting = 1;
    g_ime.result_count = 1; g_ime.candidate_idx = 0;
    kstrcpy(g_ime.results[0].kanji, "K");
    ime_feed_key(KEY_XFER << 8);
    input_ime_snapshot(&state);
    CHECK(state.active, "M7 candidate published");
    CHECK(!input_ime_cancel_capture(&state), "M7 candidate capture");
    /* A cancellation preempts even an unread tail, before any queue access. */
    g_ime.commit_buf[0] = 'Z'; g_ime.commit_len = 1; g_ime.commit_pos = 0;
    legacy_cooked_flush();
    CHECK(ime_trygetchar() == -1 && !g_ime.converting && !g_ime.commit_len,
          "M7 CUI pending before delivery");
    legacy_cooked_push('k');
    CHECK(ime_trygetkey() == -1, "M7 CUI key processed");
    input_ime_snapshot(&state);
    CHECK(state.active, "M7 CUI composition published");
    CHECK(!input_ime_cancel_capture(&state), "M7 CUI capture");
    CHECK(ime_trygetkey() == -1 && !g_ime.rk.preedit[0], "M7 empty poll cancel");
    ime_feed_key('k');
    ime_set_mode(IME_MODE_OFF);
    input_ime_snapshot(&state);
    CHECK(!state.active && ime_feed_key('x') == 'x', "M7 OFF publication");
    ime_toggle(); ime_feed_key('k');
    ime_owner_exit(2);
    input_ime_snapshot(&state);
    CHECK(!state.active, "M7 owner exit publication");
    return 0;
}
