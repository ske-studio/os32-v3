/* Confirm the selected candidate, then restore the input display. */
static void ime_candidate_commit(void)
{
    commit_candidate();
    g_ime.state = IME_ST_INPUT;
    tvram_set_scroll_reserve(1); /* インライン保護に戻す */
    candlist_clear(); /* リスト領域を消去 */
    if (g_ime.kana_len > 0) {
        preedit_draw();
    } else {
        preedit_clear();
    }
}
