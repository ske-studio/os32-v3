/* park / resume と協調 yield・STOP の状態遷移。appslot.c から順序を保って取り込む。 */

/* ======================================================================== */
/*  park / resume (D0 / D2)                                                 */
/* ======================================================================== */
int appslot_park_check(void)
{
    AppSlot *a;

    if (g_cur < APP_ID_MIN || g_cur > APP_ID_MAX) {
        /* 走っているアプリが居ない (WM top-level / シェル)。WM の規約違反。 */
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    a = appslot_get(g_cur);
    if (!a || a->state != APP_STATE_RUNNING) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    /* 塞がない起動 (exec_start) で立ったアプリだけが park できる。
     * CUI の入れ子 exec_run の子を park すると、longjmp の行き先が
     * 「走っている親アプリの中の exec_run フレーム」になり、WM top-level へ
     * 戻れない。親は子の終了まで塞がっているので譲る相手も居ない (D4)。 */
    if (!a->gui) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    /* 「OP_WAIT の中」でだけ譲る (契約 T2a、受入 G7)。他の op / X1 / X2 / X4
     * から呼ばれたら park させずに弾く — WM の行儀を信じない (D0)。 */
    if (!g_cur_op_is_wait || !a->in_op_wait) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    return 0;
}

void appslot_park_commit(void)
{
    AppSlot *a = appslot_get(g_cur);
    if (!a) return;
    /* 票 T9 §12 T1: リダイレクト表を ID の文脈として持ち替える。 */
    redir_switch_out(g_cur);
    a->parked_from_wait = 1;      /* 「OP_WAIT 由来」の印 (C5) */
    a->stop_wm_req = 0;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    a->in_op_wait = 0;
    a->state = APP_STATE_PARKED;
    g_cur_op_is_wait = 0;
    g_cur = APP_ID_SHELL;
    res_owner_set(APP_ID_SHELL);
}

/* ---- 第 2 の park 点: GUI 中の kbd 待ち (票 K7 D1) --------------------- */
/* park_check との違いは「いま gui_call(OP_WAIT) の中か」を要求しないこと
 * だけ。ここへ来るのは kbd_getchar / kbd_getkey の syscall の中で、WM の
 * コールバックではないため。GUI モードと CPL=3 フレームの有無 (票 §5 R1) は
 * drivers/input/kbd.c と exec/exec.c が見る — この表はハードウェアを知らない。 */
int appslot_park_kbd_check(void)
{
    AppSlot *a;

    if (g_cur < APP_ID_MIN || g_cur > APP_ID_MAX) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    a = appslot_get(g_cur);
    if (!a || a->state != APP_STATE_RUNNING) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    /* CUI の入れ子 exec_run の子は park できない (D4 と同じ理由: longjmp の
     * 行き先が親アプリの中の exec_run フレームになり WM へ戻れない)。
     * 呼び手は拒否されたら従来の hlt 待ちへ落ちる。 */
    if (!a->gui) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    return 0;
}

void appslot_park_kbd_commit(void)
{
    AppSlot *a = appslot_get(g_cur);
    if (!a) return;
    /* 票 T9 §12 T1: リダイレクト表を ID の文脈として持ち替える。 */
    redir_switch_out(g_cur);
    a->parked_from_kbd = 1;       /* 「kbd 待ち由来」の印 (K7 D1) */
    a->stop_wm_req = 0;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    a->in_op_wait = 0;
    a->state = APP_STATE_WAIT_KEY;
    g_cur_op_is_wait = 0;
    g_cur = APP_ID_SHELL;
    res_owner_set(APP_ID_SHELL);
    ring3_kbd_park_count++;
}

/* ---- 第 3 の park 点: ポーリング型の協調 yield (票 T8 §7 D8) ----------- */
/* park_kbd_check との違いは **PIT tick の間引き** を先に見ること。呼び手は
 * kbd_trygetchar / kbd_trygetkey で、描画ループから秒間数万回来る。間引きが
 * 無いと譲りだけで CPU を食い、間引きを表の検査の**後**に置くと、譲れない
 * 文脈 (CUI の入れ子の子) が回すたびに ring3_park_reject_count が跳ね上がる。
 * だから順番は「間引き → 表」で固定し、控えも間引きの側で進める
 * (ホスト試験ケース 22 の 22M / 22N がこの順番だけを見る)。 */
int appslot_park_poll_check(u32 now_tick)
{
    AppSlot *a;

    /* 間引き (10ms に 1 回まで)。控えるのは **成立ではなく試み** で、表の側で
     * 弾かれた試みもここで止める — 譲れない文脈 (CUI の入れ子 exec_run の子)
     * が秒間数万回ポーリングしても ring3_park_reject_count が跳ね上がらない
     * ようにするため。走るアプリは常に 1 本なので、弾かれた試みが「譲れた
     * はずの誰か」の枠を食うことはない (弾かれる文脈はそもそも譲れない)。
     * 間引き自体は違反ではないので弾き数には載せない。 */
    if (now_tick == g_poll_last_tick) return OS32_ERR_AGAIN;
    g_poll_last_tick = now_tick;

    if (g_cur < APP_ID_MIN || g_cur > APP_ID_MAX) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    a = appslot_get(g_cur);
    if (!a || a->state != APP_STATE_RUNNING) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    /* CUI の入れ子 exec_run の子は譲れない (park_kbd_check と同じ理由:
     * longjmp の行き先が親アプリの中の exec_run フレームになる)。 */
    if (!a->gui) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    return 0;
}

void appslot_park_poll_commit(void)
{
    AppSlot *a = appslot_get(g_cur);
    if (!a) return;
    /* 票 T9 §12 T1: リダイレクト表を ID の文脈として持ち替える。 */
    redir_switch_out(g_cur);
    a->parked_from_poll = 1;      /* 「ポーリング由来」の印 (T8 D8) */
    a->stop_wm_req = 0;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    a->in_op_wait = 0;
    a->state = APP_STATE_WAIT_POLL;
    g_cur_op_is_wait = 0;
    g_cur = APP_ID_SHELL;
    res_owner_set(APP_ID_SHELL);
    /* 控え (g_poll_last_tick) を進めるのは check の側 — 弾かれた試みも
     * 間引きたいので、成立した分だけでは足りない。 */
    ring3_poll_yield_count++;
}

void appslot_poll_yield_reset(void)
{
    g_poll_last_tick = 0;
}

/* ---- 第 4 の park 点: 明示的な譲り sys_yield (票 T9 D5) --------------- */
/* park_poll_check との違いは **間引きを掛けない** こと。sys_yield は
 * 「いま譲る」と書いた呼び手の意思で、kbd_trygetchar の busy-wait とは
 * 性質が違う (sh は launch_poll の合間に 1 回ずつしか呼ばない)。間引くと
 * 同じ tick の中で sh が回り続け、譲りが成立しないまま CPU を食う。 */
int appslot_park_yield_check(void)
{
    AppSlot *a;

    if (g_cur < APP_ID_MIN || g_cur > APP_ID_MAX) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    a = appslot_get(g_cur);
    if (!a || a->state != APP_STATE_RUNNING) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    /* CUI の入れ子 exec_run の子は譲れない (park_kbd_check と同じ理由:
     * longjmp の行き先が親アプリの中の exec_run フレームになる)。 */
    if (!a->gui) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }
    return 0;
}

void appslot_park_yield_commit(void)
{
    AppSlot *a = appslot_get(g_cur);
    if (!a) return;
    /* 票 T9 §12 T1: リダイレクト表を ID の文脈として持ち替える。 */
    redir_switch_out(g_cur);
    a->parked_from_yield = 1;     /* 「明示的な譲り由来」の印 (T9 D5) */
    a->stop_wm_req = 0;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    a->in_op_wait = 0;
    a->state = APP_STATE_WAIT_POLL;
    g_cur_op_is_wait = 0;
    g_cur = APP_ID_SHELL;
    res_owner_set(APP_ID_SHELL);
    ring3_yield_count++;
}

/* STOP never chooses an owner in IRQ context. The WM resolves the target. */
void appslot_stop_request(void)
{
    AppSlot *a = appslot_get(g_cur);
    /* IRQ can arrive after WM capture, immediately before resume/start.
     * Keep that request in the shell slot until WM consumes it or the next
     * completed syscall returns control. It has no application target yet. */
    if (g_cur == APP_ID_SHELL) {
        g_slot[APP_ID_SHELL].stop_wm_req = 1;
        return;
    }
    if (a && g_cur >= APP_ID_MIN && a->gui && a->cpl3 &&
        a->state == APP_STATE_RUNNING && !a->in_op_wait)
        a->stop_wm_req = 1;
}

int appslot_stop_pending(void)
{
    AppSlot *a = appslot_get(g_cur);
    return g_slot[APP_ID_SHELL].stop_wm_req || (a && a->stop_wm_req);
}

void appslot_park_stop_commit(void)
{
    AppSlot *a = appslot_get(g_cur);
    if (!a) return;
    redir_switch_out(g_cur);
    a->stop_wm_req = 0;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    ring3_stop_park_count++;
    a->parked_from_stop = 1;
    a->in_op_wait = 0;
    a->state = APP_STATE_WAIT_POLL;
    g_cur_op_is_wait = 0;
    g_cur = APP_ID_SHELL;
    res_owner_set(APP_ID_SHELL);
}

int appslot_resume_check(int id)
{
    AppSlot *a;

    /* resume を呼ぶのは WM の top-level。走っているアプリの横取りは無い。 */
    if (g_cur != APP_ID_SHELL) return OS32_ERR_INVAL;
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    a = appslot_get(id);
    if (!a) return OS32_ERR_INVAL;                  /* 未知 / 畳まれた ID */
    /* 印の無いフレームは起こさない (C6)。ここが受入 G7 / K7 の I5 の合否
     * そのもので、park 点が 2 つになっても規則は 1 つ — 「その状態に対応する
     * 印が立っているフレームだけ」。 */
    if (a->state == APP_STATE_WAIT_KEY) {
        if (!a->parked_from_kbd) {
            ring3_resume_bad_frame_count++;
            return OS32_ERR_STALE;
        }
        return 0;
    }
    /* ポーリング型の譲り (T8 D8)。起こす条件は WAIT_KEY と違って
     * 「注入リングに文字がある」ではない — WM は次の周に必ず起こし、
     * 空なら exec_resume が EAX に -1 を書く。ここで見るのは印だけ。 */
    if (a->state == APP_STATE_WAIT_POLL) {
        /* WAIT_POLL は polling / yield / STOP 完了の 3 由来。印が無ければ
         * 起こさない — 規則は park 点が増えても 1 つ: 「その状態に対応する
         * 印が立っているフレームだけ」。 */
        if (!a->parked_from_poll && !a->parked_from_yield && !a->parked_from_stop) {
            ring3_resume_bad_frame_count++;
            return OS32_ERR_STALE;
        }
        return 0;
    }
    if (a->state != APP_STATE_PARKED) return OS32_ERR_INVAL;
    if (!a->parked_from_wait) {
        ring3_resume_bad_frame_count++;
        return OS32_ERR_STALE;
    }
    return 0;
}

/* resume のとき EAX に何を入れるかを印から導く (票 T9 D5)。印と「どこから
 * 値を取るか」の対応表をここ 1 か所に閉じると、exec_resume 側は
 * ハードウェア (注入リング / フレーム) の操作だけになる。 */
int appslot_resume_source(int id)
{
    AppSlot *a = appslot_get(id);
    if (!a) return OS32_ERR_INVAL;
    if (a->state == APP_STATE_WAIT_KEY)  return APP_RESUME_SRC_KBD;
    if (a->state == APP_STATE_WAIT_POLL) {
        /* 明示的な譲りは注入リングを読まない — 読むと、譲っている sh が
         * 子宛の 1 バイトを吸って捨てる (票 §6 blocker 1)。 */
        if (a->parked_from_stop) return APP_RESUME_SRC_KEEP;
        if (a->parked_from_yield) return APP_RESUME_SRC_YIELD;
        return APP_RESUME_SRC_POLL;
    }
    return APP_RESUME_SRC_WAIT;
}

void appslot_resume_commit(int id)
{
    AppSlot *a = appslot_get(id);
    if (!a) return;
    /* 票 T9 §12 T1: この ID が park 前に持っていた表へ戻す。 */
    redir_switch_in(id);
    a->parked_from_wait = 0;      /* 印は 1 回きり (3 つの park 点すべてで) */
    a->parked_from_kbd = 0;
    a->parked_from_poll = 0;
    a->parked_from_yield = 0;
    a->parked_from_stop = 0;
    a->stop_wm_req = 0;
    a->last_kernel_tick = 0;
    a->in_op_wait = 0;
    a->state = APP_STATE_RUNNING;
    g_cur = id;
    g_cur_op_is_wait = 0;
    res_owner_set(id);
    /* 受入 G7 が数えるのはこれ = park してある生存アプリを起こした回数。 */
    ring3_switch_count++;
}

