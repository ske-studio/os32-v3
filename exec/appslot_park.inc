/* OP_WAIT / kbd / poll / yield の共通更新。由来・状態・計数は各入口が選ぶ。 */
static void appslot_park_commit_origin(AppSlot *a, int *origin, int state)
{
    /* 票 T9 §12 T1: リダイレクト表を ID の文脈として持ち替える。 */
    redir_switch_out(g_cur);
    *origin = 1;
    a->stop_wm_req = 0;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    appslot_park_finish(a, state);
}
