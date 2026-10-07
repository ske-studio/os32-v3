/* ======================================================================== */
/*  APPSLOT.C — アプリ ID の表と状態遷移 (K5b-K)                             */
/*                                                                          */
/*  票 docs/archive/gui_v13/TASK_K5_multiapp.md の §設計 D0〜D4 をそのまま      */
/*  写したもの。K5a のホスト模型 (tools/tests/multiapp_model_host.c) の       */
/*  遷移表が仕様で、この .c がその実装。同じ検査を                            */
/*  tools/tests/multiapp_impl_host.c が **この .c を直接コンパイルして** 回す。*/
/*                                                                          */
/*  ここに置かないもの (置くと模型がホストで動かなくなる):                    */
/*    - CR3 / setjmp / pgalloc / ページテーブル → exec/exec.c                */
/*    - 「次に誰を起こすか」の規則 → gshell (D11-5: カーネルは順番を決めない) */
/*    - SHM スロットの割当 → gshell の alloc_slot (D6)                       */
/* ======================================================================== */

#include "appslot.h"
#include "pgalloc.h"       /* §6 ownership/mapping management target sizeof */
#include "os32_kapi_shared.h"   /* OS32_ERR_* / EXEC_ERR_* */
#include "fd_redirect.h"       /* T9 §12 T1: リダイレクト表を ID の文脈にする */

/* res_owner_set/get は fs/fd_redirect.c。exec/ は -Ifs を持たないので
 * kernel/gui.c と同じ流儀で extern 宣言する。 */
extern void res_owner_set(int owner);
extern int  res_owner_get(void);

/* ID 1 = シェル帯は kernel/gui.h の GUI_SHELL_OWNER と同じ値でなければ
 * ならない (gui_register / sys_switch_shell の owner 判定がこれを見る)。
 * gui.h を include すると exec/ が -Ikernel に依存するので値で固定する。 */
STATIC_ASSERT(APP_ID_SHELL == 1, appslot_shell_id_is_gui_shell_owner);
STATIC_ASSERT(APP_ID_MAX < APP_SLOT_COUNT, appslot_table_holds_id_max);

/* TASK_T2_APPBAND §6: all six slots plus four KHEAP AS control blocks.
 * PFN metadata and actual PD/PT backing have separate physical budgets. */
STATIC_ASSERT(sizeof(struct addrspace) <= 1376, as_control_within_1376);
STATIC_ASSERT(sizeof(AppSlot) * APP_SLOT_COUNT +
              sizeof(struct addrspace) * (APP_ID_MAX - APP_ID_SHELL) +
              sizeof(struct ledger_owner) * LEDGER_MAX_OWNERS +
              sizeof(struct ledger_region) * LEDGER_MAX_REGIONS +
              sizeof(struct ledger_resource) * LEDGER_MAX_RESOURCES +
              sizeof(struct ledger_surface) * LEDGER_MAX_SURFACES <= 16UL * 1024,
              ownership_mapping_management_within_16k);

/* 標準 FD のリダイレクト表 (fs/fd_redirect.c) の **ID ごとの枠** (票 T9 §12 T1)。
 * 表は FD 0/1/2 の 3 本しかなく全アプリ共有だったので、park してある sh の
 * `> /tmp/out` に別アプリの printf が入り、パイプ中なら sh の .bss (別 CR3 で
 * 解決される仮想番地) を別アプリが書いていた。park で走っていた ID の枠へ
 * 移し、resume で戻す。添字 = ID で、ID 1 (WM / 常駐シェル) の枠も同じ表に
 * 置く — 走っているのは常に 1 本なので、生きている表は「いまの表」1 つだけ。
 *
 * 境界: ID 1 が stdio を張ったまま exec_start することは無い (gshell は
 * stdio を張らず、常駐シェルは入れ子 exec_run で park しない)。もし張れば
 * その枠は子の枠へ移り、子の回収で閉じられる — 表が FD ごとに 1 本しかない
 * という元からの限界 (D3) と同じ性質の話。 */
static FdRedirectState g_redir[APP_SLOT_COUNT];

/* 走っている id が譲る: いまの表を id の枠へ移し、WM の枠を表へ戻す。 */
static void redir_switch_out(int id)
{
    fd_redirect_save(&g_redir[id]);
    fd_redirect_restore(&g_redir[APP_ID_SHELL]);
    fd_redirect_clear_state(&g_redir[APP_ID_SHELL]);   /* 所有は表へ移った */
}

/* id を起こす: いまの表 (WM のもの) を WM の枠へ移し、id の枠を表へ戻す。 */
static void redir_switch_in(int id)
{
    fd_redirect_save(&g_redir[APP_ID_SHELL]);
    fd_redirect_restore(&g_redir[id]);
    fd_redirect_clear_state(&g_redir[id]);             /* 所有は表へ移った */
}

volatile u32 ring3_switch_count = 0;
volatile u32 ring3_transition_count = 0;
volatile u32 ring3_park_reject_count = 0;
volatile u32 ring3_resume_bad_frame_count = 0;
volatile u32 ring3_kbd_park_count = 0;
volatile u32 ring3_poll_yield_count = 0;
volatile u32 ring3_yield_count = 0;
volatile u32 ring3_stop_park_count = 0;
volatile u32 appslot_reclaim_count = 0;
volatile int appslot_last_reclaim_id = 0;
volatile u32 gfx_init_reject_count = 0;

/* 画面の所有者 (票 T8 D1)。初期値は WM。CUI 中は誰も取らないのでここに
 * 留まる。static にしないのは kernel.map から emu_read_mem で読むため
 * (fault_kill_count と同じ流儀)。 */
volatile int g_gfx_owner = GFX_OWNER_WM;

static AppSlot g_slot[APP_SLOT_COUNT];
static int g_cur = APP_ID_SHELL;

/* gui_call がハンドラを呼ぶ間だけ立つ「いまの op は OP_WAIT か」(C4)。
 * park の可否と印の判定に使う。gui_call は入れ子にならない (契約 T1: WM から
 * アプリへのコールバック経路を作らない) ので 1 本で足りる。 */
static int g_cur_op_is_wait = 0;

/* ポーリング型 yield (票 T8 §7 D8) の間引き: 最後に譲った PIT tick。
 * 「前回の譲りから tick が進んでいる」ときだけ譲る = 100Hz なので 10ms に
 * 1 回まで。表全体で 1 語 — 走っているアプリは常に 1 本なので、スロット
 * ごとに持つ意味がない。比較を `!=` にしてあるのは u32 の一周
 * (100Hz で 497 日) を跨いでも止まらないため (`>` だと一周の瞬間に
 * 譲りが永久に止まる)。 */
static u32 g_poll_last_tick = 0;

static void slot_zero(AppSlot *a)
{
    u8 *b = (u8 *)a;
    u32 i;
    for (i = 0; i < (u32)sizeof(AppSlot); i++) b[i] = 0;
}

void appslot_init(void)
{
    int i;

    /* GUI セッションを跨いで古い tick を引きずらない (票 T8 D8)。 */
    g_poll_last_tick = 0;
    for (i = 0; i < APP_SLOT_COUNT; i++) {
        slot_zero(&g_slot[i]);
        g_slot[i].state = APP_STATE_FREE;
    }
    /* シェル帯 (ID 1) は常に居る。depth は従来の exec_nest_level = 1。 */
    g_slot[APP_ID_SHELL].state = APP_STATE_RUNNING;
    g_slot[APP_ID_SHELL].parent = 0;
    g_slot[APP_ID_SHELL].depth = 1;
    g_cur = APP_ID_SHELL;
    g_cur_op_is_wait = 0;
    g_gfx_owner = GFX_OWNER_WM;   /* 画面は WM のもの (票 T8 D1) */
    /* リダイレクトの枠も空から (票 T9 §12 T1)。いまの表は fd_redirect_init
     * が別に空にする — ここで閉じると、まだ生きている FD を横から閉じる。 */
    for (i = 0; i < APP_SLOT_COUNT; i++) fd_redirect_clear_state(&g_redir[i]);
    res_owner_set(APP_ID_SHELL);
}

AppSlot *appslot_at(int id)
{
    if (id < 0 || id >= APP_SLOT_COUNT) return (AppSlot *)0;
    return &g_slot[id];
}

AppSlot *appslot_get(int id)
{
    AppSlot *a = appslot_at(id);
    if (!a || a->state == APP_STATE_FREE) return (AppSlot *)0;
    return a;
}

int appslot_cur(void) { return g_cur; }

int appslot_live(void)
{
    int i, n = 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        if (g_slot[i].state != APP_STATE_FREE) n++;
    }
    return n;
}

/* 空き ID は必ず**小さい方から**。これで CUI の入れ子 exec_run が
 * 2, 3, 4 と並び、「段 = ID」という従来の見え方がそのまま残る (D3)。 */
int appslot_alloc_id(void)
{
    int i;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        if (g_slot[i].state == APP_STATE_FREE) return i;
    }
    return 0;
}

/* ======================================================================== */
/*  起動 (D4)                                                               */
/* ======================================================================== */

/* CUI 専用の宣言 (票 T8-2)。v86 は CPL=3 なので --cpl0 の網には掛からない —
 * 宣言ビットを見る枝をここに 1 本足して、GUI からの起動だけを断つ。 */
int appslot_cui_only_admit(int gui, u32 hdr_flags)
{
    if (!gui) return 0;                          /* CUI は従来どおり */
    if (hdr_flags & OS32X_FLAG_CUI_ONLY) return OS32_ERR_INVAL;
    return 0;
}

int appslot_start_admit(int gui, u32 pages, u32 free_pages)
{
    int id;

    /* GUI アプリ (塞がない起動) は WM の top-level からだけ — 契約 S2。
     * CUI の入れ子 exec_run は従来どおり走っているアプリからも通る (D9-8)。 */
    if (gui && g_cur != APP_ID_SHELL) return OS32_ERR_INVAL;

    id = appslot_alloc_id();
    if (id == 0) return OS32_ERR_FULL;          /* 5 本目 (受入 G3) */

    /* 入らなければ拒否する。切り詰めない・スワップしない (D5)。
     * free_pages == 0 は「ここでは勘定しない」の合図 (CPL=0 の子は
     * pgalloc_mark_used で従来どおり固定帯を押さえる)。 */
    if (free_pages != 0 && pages > free_pages) return EXEC_ERR_NOMEM;

    return id;
}

void appslot_start_commit(int id, int gui, u32 pages)
{
    AppSlot *a = appslot_at(id);
    AppSlot *parent = appslot_at(g_cur);
    if (!a) return;
    a->state = APP_STATE_RUNNING;
    a->parent = g_cur;
    a->depth = (parent ? parent->depth : 1) + 1;
    a->gui = gui;
    a->pages = pages;
    a->in_op_wait = 0;
    a->abort_req = 0;
    a->parked_from_wait = 0;
    a->parked_from_kbd = 0;
    a->parked_from_poll = 0;
    a->parked_from_yield = 0;
    a->parked_from_stop = 0;
    a->stop_wm_req = 0;
    a->last_kernel_tick = 0;
    g_cur = id;
    res_owner_set(id);
    /* 起動の iret は「生存アプリの集合が変わる瞬間」で、生存アプリ間の
     * 実行切替ではない (D0)。受入 G7 が数える switch とは別勘定にする。 */
    ring3_transition_count++;
}

void appslot_shell_commit(void)
{
    AppSlot *a = &g_slot[APP_ID_SHELL];
    a->state = APP_STATE_RUNNING;
    a->parent = 0;
    a->depth = 1;
    a->gui = 0;
    a->cpl3 = 0;
    a->in_op_wait = 0;
    a->abort_req = 0;
    a->parked_from_wait = 0;
    a->parked_from_kbd = 0;
    a->parked_from_poll = 0;
    a->parked_from_yield = 0;
    a->parked_from_stop = 0;
    a->stop_wm_req = 0;
    a->last_kernel_tick = 0;
    g_cur = APP_ID_SHELL;
    g_cur_op_is_wait = 0;
    /* シェル帯を載せ替えた (shell.bin ⇄ gshell.bin)。前の住人の枠は
     * その ID 1 の退場で回収済みなので、枠だけ空に戻す (票 T9 §12 T1)。 */
    fd_redirect_clear_state(&g_redir[APP_ID_SHELL]);
    res_owner_set(APP_ID_SHELL);
}

/* ======================================================================== */
/*  gui_call の文脈 (C4)                                                    */
/* ======================================================================== */
void appslot_gui_op_enter(int is_wait)
{
    AppSlot *a = appslot_get(g_cur);
    g_cur_op_is_wait = is_wait ? 1 : 0;
    if (a) a->in_op_wait = g_cur_op_is_wait;
}

void appslot_gui_op_leave(void)
{
    AppSlot *a = appslot_get(g_cur);
    g_cur_op_is_wait = 0;
    if (a) a->in_op_wait = 0;
}

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
 * drivers/kbd.c と exec/exec.c が見る — この表はハードウェアを知らない。 */
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

/* ======================================================================== */
/*  終了・kill (D4)                                                         */
/* ======================================================================== */
u32 appslot_reclaim(int id)
{
    AppSlot *a = appslot_get(id);
    u32 pages;
    if (!a || id == APP_ID_SHELL) return 0;
    pages = a->pages;
    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    /* 票 T9 §12 T1: 枠は **閉じずに空にするだけ**。枠の中の file_fd は
     * fd_redirect_to_file の vfs_open がこの ID の owner タグを付けて取った
     * ものなので、park したまま畳まれても exec_reclaim_owned の
     * vfs_close_owned(id) が閉じる (往復 9 の non-blocker: ここで閉じると
     * その後 vfs_close_owned が同じ FD をもう一度閉じていた)。
     * バッファ型の枠に FD は無く、バッファ自体は pipe_free_owned が返す。 */
    fd_redirect_clear_state(&g_redir[id]);
    slot_zero(a);
    a->state = APP_STATE_FREE;
    appslot_reclaim_count++;
    appslot_last_reclaim_id = id;
    return pages;
}

int appslot_return_target(int id)
{
    AppSlot *a = appslot_at(id);
    int parent = a ? a->parent : APP_ID_SHELL;
    if (parent >= APP_ID_MIN && parent <= APP_ID_MAX &&
        g_slot[parent].state != APP_STATE_FREE) return parent;
    return APP_ID_SHELL;
}

void appslot_switch_to(int id)
{
    AppSlot *a = appslot_at(id);
    if (!a) return;
    g_cur = id;
    g_cur_op_is_wait = 0;
    if (id != APP_ID_SHELL) a->state = APP_STATE_RUNNING;
    res_owner_set(id);
    ring3_transition_count++;
}

int appslot_kill_check(int id)
{
    AppSlot *a;
    if (g_cur != APP_ID_SHELL) return OS32_ERR_INVAL;
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    a = appslot_get(id);
    if (!a) return OS32_ERR_INVAL;
    /* 走っている本人は CTRL+STOP の経路で畳む (D4)。止めてある側は
     * OP_WAIT 由来 (PARKED) でも kbd 待ち (WAIT_KEY) でもポーリングの譲り
     * (WAIT_POLL) でも畳める — 止めてあるアプリを永久に畳めないと
     * CTRL+STOP の逃げ道が無くなる (票 K7 D5 / T8 D8)。 */
    if (a->state != APP_STATE_PARKED && a->state != APP_STATE_WAIT_KEY &&
        a->state != APP_STATE_WAIT_POLL) {
        return OS32_ERR_STALE;
    }
    return 0;
}

int appslot_abort_request(void)
{
    AppSlot *a = appslot_get(g_cur);
    /* IRQ1 が知っているのは「いま走っているアプリ」だけ。止めてあるアプリ
     * には届かない — そちらは exec_kill で畳む (D4)。 */
    if (!a || g_cur < APP_ID_MIN || a->state != APP_STATE_RUNNING) return 0;
    a->abort_req = 1;
    return 1;
}

/* ---- IRQ1 由来の CTRL+STOP を立ててよいか (票 T9 §12 S6) --------------- */
/* GUI 配下では宛先が「フォーカス窓の連鎖の末尾」(D8) に変わった。それを
 * 解決できるのは窓の所有者を知っている WM だけ。**ただし WM がそのアプリの
 * gui_call(OP_WAIT) の中で回っているとき (in_op_wait) は、要求を立てても
 * 必ず WM のハンドラが先に見る**ので従来どおり通す (K5c)。問題になるのは
 * 「CPL=3 のアプリのコードが実際に走っている最中」で、IRQ1 は「そのとき
 * 走っていた slot」しか知らない。T9 で sh が WAIT_POLL で毎 tick 回り、端末も
 * 100ms タイマで回るようになったので、IRQ1 が落ちた先はほぼ常に**宛先と
 * 無関係なアプリ**になる (受入 S6: 2 回目の CTRL+STOP で端末まで畳まれ、
 * ring3_abort_count が +1 した)。だから **GUI 中にアプリのコードを割り込んだ
 * ときだけ**カーネルは立てない — 要求は raw リング経由で WM に届き、WM が
 * exec_abort_clear → 末尾を exec_kill する (決裁 A1 / D8)。
 *
 * 残す例外は 1 つだけ: **暴走**。KAPI を呼ばない計算ループに入ったアプリは
 * WM へ戻らないので、WM は制御を取り戻せず CTRL+STOP も届かない。
 * 「最後に **カーネルへ入って** から APP_RUNAWAY_TICKS 以上」なら協調型が
 * 壊れているので、従来どおり IRQ1 が畳む。起点を start / resume ではなく
 * syscall 入口にするのが要点 (§12 S6b): GetMessage 型のアプリは WM の
 * op_wait の中で待つ間 resume を通らないので、resume 起点だと「2 秒待った
 * だけ」で暴走に見え、D8 の宛先より先に畳まれてしまう。
 *
 * gfx 拒否 (appslot_gfx_claim) と V86 の脱出は appslot_abort_request() を
 * 直に呼ぶ — あちらは「WM / カーネルが宛先を決めた」kill なので、この関門は
 * 通らない (GUI 中でも従来どおり効く)。 */
int appslot_abort_admit(int gui_mode, u32 now_tick)
{
    AppSlot *a;

    if (!gui_mode) return 1;              /* CUI は 1 バイトも変えない (K2) */

    a = appslot_get(g_cur);
    if (!a || g_cur < APP_ID_MIN) return 0;          /* WM / シェル帯 */
    if (a->state != APP_STATE_RUNNING) return 0;

    /* **K5c の経路はそのまま通す**: `in_op_wait` = WM がこのアプリの
     * gui_call(OP_WAIT) の**中**で回っている (kernel/gui.c の gui_call が
     * ハンドラを呼ぶ間だけ立つ)。このとき割り込まれた文脈は CPL=0 (WM の
     * コード) なので IRQ1 スタブの即 kill は起きず、要求は必ず WM の
     * ハンドラが見る — WM が宛先を解決し、本人なら break して syscall 出口の
     * ring3_abort_check が畳み、別なら exec_abort_clear で降ろして
     * exec_kill(宛先) する (決裁 A1 / D8)。つまり「カーネルが宛先を決めて
     * しまう」害は無く、素の GUI アプリ (gui_demo 等) をフォーカスして
     * CTRL+STOP で閉じる K5b/K5c の挙動もここで生きる。
     * ここを塞いだ版では、連鎖の末尾が端末自身のとき (3 回目の CTRL+STOP)
     * に誰も畳まなくなった (受入 S6 の再試験)。 */
    if (a->in_op_wait) return 1;

    /* ここから先は「CPL=3 のアプリのコードが実際に走っている最中」。
     * 立てると IRQ1 スタブが D8 の宛先より先に畳むので、暴走のときだけ。
     * u32 の引き算なので tick が一周しても正しい差が出る。 */
    if ((u32)(now_tick - a->last_kernel_tick) >= (u32)APP_RUNAWAY_TICKS) {
        return 1;
    }
    return 0;
}

void appslot_mark_scheduled(int id, u32 now_tick)
{
    AppSlot *a = appslot_get(id);
    if (!a) return;
    a->last_kernel_tick = now_tick;
}

/* ======================================================================== */
/*  appslot_abort_clear — CTRL+STOP の要求を降ろす (KAPI v45、決裁 A1)       */
/*                                                                          */
/*  IRQ1 は宛先を選べないので「いま走っているアプリ」に無条件で立てる        */
/*  (appslot_abort_request)。契約 T6 の宛先はフォーカス窓のアプリなので、    */
/*  別アプリが走っていたときは WM がこれで要求を降ろし、フォーカス窓の ID を */
/*  exec_kill で畳む。降ろさないと「意図しない 1 本が次の syscall で死ぬ」。 */
/*                                                                          */
/*  要求を負えるのは走っている 1 本だけなので対象は高々 1 本。ただし WM が   */
/*  top-level (owner 1) へ戻るのは park の後なので、そのときスロットの状態は */
/*  PARKED になっている — 状態では絞らず「要求を負っている ID」で探す。      */
/*  降ろすのは abort_req だけで、state / in_op_wait / parked_from_wait と    */
/*  他の ID のスロットには触らない。                                         */
/* ======================================================================== */
int appslot_abort_clear(void)
{
    int i;

    /* gui_register と同じ判定: シェル帯 (owner 1) からのみ。 */
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;

    g_slot[APP_ID_SHELL].stop_wm_req = 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        if (g_slot[i].state != APP_STATE_FREE) {
            /* WM clears keyboard STOP requests, never a quarantine abort. */
            if (!g_slot[i].as || !g_slot[i].as->appmem_poisoned)
                g_slot[i].abort_req = 0;
            g_slot[i].stop_wm_req = 0;
        }
    }
    return 0;
}

int appslot_state(int id)
{
    AppSlot *a;
    if (id < APP_ID_MIN || id > APP_ID_MAX) return OS32_ERR_INVAL;
    a = &g_slot[id];
    if (a->state == APP_STATE_FREE) return 0;
    if (a->state == APP_STATE_RUNNING) return 1;
    /* 3 = kbd 待ち (票 K7 §5 指摘 C の値の追加)。2 の意味は動かさない。 */
    if (a->state == APP_STATE_WAIT_KEY) return APP_STATE_WAIT_KEY;
    /* 4 = ポーリングの譲り (票 T8 D8)。WM は「常に ready、優先度は最下位」
     * として扱う — 3 と違い、注入リングが空でも起こしてよい。 */
    if (a->state == APP_STATE_WAIT_POLL) return APP_STATE_WAIT_POLL;
    return 2;
}

/* ======================================================================== */
/*  画面の所有者 (票 T8 D1 / D1a)                                            */
/*                                                                          */
/*  全画面 GFX は「1 枚の画面を丸ごと持っていく」操作なので、持ち主を        */
/*  カーネルが 1 つだけ覚える。取るのは gfx_init / gfx_init_200 の KAPI      */
/*  ラッパ (gfx/gfx_core.c) だけで、返すのは回収 (exec_reclaim_owned) だけ。 */
/*                                                                          */
/*  判定を純関数に切り出してあるのは、材料 (GUI 中か / 呼び手の ID / CPL /   */
/*  ヘッダの宣言ビット) が全部この表にあり、ホストでそのまま試験できるため   */
/*  (tools/tests/multiapp_impl_host.c ケース 20)。                           */
/* ======================================================================== */
int appslot_gfx_claim_check(int gui_mode, int caller, int cpl3, u32 hdr_flags)
{
    /* CUI 中 (con_sink 無効) は所有者を触らない。gshell は居らず、
     * 全画面は従来どおり誰でも取れる (票 D1a「CUI 中は何でも通す」)。 */
    if (!gui_mode) return 0;
    /* WM 自身 (シェル帯) と、表に無い ID は素通し。復帰の gfx_init は
     * ここを通る。 */
    if (caller < APP_ID_MIN || caller > APP_ID_MAX) return 0;
    /* CPL=0 の子 (--cpl0) は所有者を取らない。GUI からの起動は
     * 非シェルはローダが CPL=3 のみ許可する。 */
    if (!cpl3) return 0;
    /* 宣言 (mkos32x --gfx) が無ければ画面を渡さない (D1a)。黙って
     * 画面を壊させるより、gfx_init を呼ばずに断る。 */
    if ((hdr_flags & OS32X_FLAG_GFX) == 0) return OS32_ERR_INVAL;
    return caller;
}

int appslot_gfx_claim(int gui_mode)
{
    int caller = res_owner_get();
    AppSlot *a = appslot_get(caller);
    int r = appslot_gfx_claim_check(gui_mode, caller,
                                    a ? a->cpl3 : 0,
                                    a ? a->hdr_flags : 0);
    if (r < 0) {
        gfx_init_reject_count++;
        /* 票 T8-2 (PM 実測 2026-09-12、受入 F6): 拒否して **続行させる**と、
         * プログラムは gfx_init が失敗したことを知らないまま描画 KAPI と
         * VRAM 直書きで描き続け、GUI を壊す (reject 2 回の後に FPS 計測の絵が
         * 上 200 ラインへ出た)。拒否 = そのアプリを畳む。
         * 走っている本人なので exec_kill は使えない — K5c の CTRL+STOP と
         * 同じく abort_req を立て、syscall 出口の ring3_abort_check() に
         * 畳ませる。数えるのは gfx_init_reject_count のまま (専用カウンタは
         * 増やさない)。 */
        appslot_abort_request();
        return r;
    }
    if (r > 0) g_gfx_owner = r;
    return 0;
}

int appslot_gfx_owner(void) { return g_gfx_owner; }

void appslot_gfx_owner_exit(int id)
{
    if (g_gfx_owner == id) g_gfx_owner = GFX_OWNER_WM;
}

/* ======================================================================== */
/*  appslot_resume_mark_selftest — 「印の無い resume は拒否」の負例 (I5)     */
/*                                                                          */
/*  票 K7 の受入 I5 の半分。park 点が 3 つになったので、C6 の規則             */
/*  (「その状態に対応する印が立っているフレームだけ起こせる」) が            */
/*  PARKED / WAIT_KEY / WAIT_POLL の**すべて**に効いていることと、D8 の     */
/*  tick の間引きが弾き数より先に効くことをブート時に踏む。                 */
/*                                                                          */
/*  空きスロット (APP_ID_MAX) を一時的に借りる。kselftest_run は exec_init   */
/*  より前に走るので g_slot は全部 FREE だが、順序に頼らず借りた中身と        */
/*  cur / owner / カウンタを丸ごと保存して戻す。                             */
/* ======================================================================== */
u32 appslot_resume_mark_selftest(void)
{
    u32 bad = 0;
    int id = APP_ID_MAX;
    AppSlot saved;
    int saved_cur = g_cur;
    int saved_owner = res_owner_get();
    u32 saved_badframe = ring3_resume_bad_frame_count;
    u32 saved_switch = ring3_switch_count;

    saved = g_slot[id];
    g_cur = APP_ID_SHELL;
    res_owner_set(APP_ID_SHELL);
    slot_zero(&g_slot[id]);

    /* (0) 空きスロットは起こせない */
    g_slot[id].state = APP_STATE_FREE;
    if (appslot_resume_check(id) != OS32_ERR_INVAL) bad |= 1u << 0;

    /* (1) OP_WAIT 由来: 印が無ければ STALE、あれば 0 */
    g_slot[id].state = APP_STATE_PARKED;
    g_slot[id].parked_from_wait = 0;
    g_slot[id].parked_from_kbd = 0;
    if (appslot_resume_check(id) != OS32_ERR_STALE) bad |= 1u << 1;
    if (ring3_resume_bad_frame_count != saved_badframe + 1) bad |= 1u << 1;
    g_slot[id].parked_from_wait = 1;
    if (appslot_resume_check(id) != 0) bad |= 1u << 1;

    /* (2) kbd 待ち: kbd の印が要る。OP_WAIT の印では起こせない */
    g_slot[id].state = APP_STATE_WAIT_KEY;
    g_slot[id].parked_from_wait = 1;
    g_slot[id].parked_from_kbd = 0;
    if (appslot_resume_check(id) != OS32_ERR_STALE) bad |= 1u << 2;
    if (ring3_resume_bad_frame_count != saved_badframe + 2) bad |= 1u << 2;
    g_slot[id].parked_from_kbd = 1;
    if (appslot_resume_check(id) != 0) bad |= 1u << 2;

    /* (3) 鍵待ちは畳める (D5)、走っている本人は畳めない */
    if (appslot_kill_check(id) != 0) bad |= 1u << 3;
    g_slot[id].state = APP_STATE_RUNNING;
    if (appslot_kill_check(id) != OS32_ERR_STALE) bad |= 1u << 3;

    /* (4) exec_app_state は 3 / 4 を返す (既存の 0/1/2 は不変) */
    g_slot[id].state = APP_STATE_WAIT_KEY;
    if (appslot_state(id) != APP_STATE_WAIT_KEY) bad |= 1u << 4;
    g_slot[id].state = APP_STATE_WAIT_POLL;
    if (appslot_state(id) != APP_STATE_WAIT_POLL) bad |= 1u << 4;
    g_slot[id].state = APP_STATE_RUNNING;
    if (appslot_state(id) != 1) bad |= 1u << 4;
    g_slot[id].state = APP_STATE_PARKED;
    if (appslot_state(id) != 2) bad |= 1u << 4;
    g_slot[id].state = APP_STATE_FREE;
    if (appslot_state(id) != 0) bad |= 1u << 4;

    /* (6) 第 3 の park 点 (票 T8 §7 D8): 印 parked_from_poll が要る。
     * OP_WAIT / kbd の印では起こせない。畳むのは WAIT_KEY と同じく可 (D5)。 */
    g_slot[id].state = APP_STATE_WAIT_POLL;
    g_slot[id].parked_from_wait = 1;
    g_slot[id].parked_from_kbd = 1;
    g_slot[id].parked_from_poll = 0;
    if (appslot_resume_check(id) != OS32_ERR_STALE) bad |= 1u << 6;
    if (ring3_resume_bad_frame_count != saved_badframe + 3) bad |= 1u << 6;
    g_slot[id].parked_from_poll = 1;
    if (appslot_resume_check(id) != 0) bad |= 1u << 6;
    if (appslot_kill_check(id) != 0) bad |= 1u << 6;

    /* (8) 第 4 の park 点 (票 T9 D5): 明示的な譲り。状態は WAIT_POLL のまま
     * だが、印が parked_from_yield なら resume は注入リングを読まない。
     * 印が 1 つも無ければ起こせないのは他の park 点と同じ。 */
    g_slot[id].state = APP_STATE_WAIT_POLL;
    g_slot[id].parked_from_poll = 0;
    g_slot[id].parked_from_yield = 0;
    if (appslot_resume_check(id) != OS32_ERR_STALE) bad |= 1u << 8;
    if (ring3_resume_bad_frame_count != saved_badframe + 4) bad |= 1u << 8;
    g_slot[id].parked_from_yield = 1;
    if (appslot_resume_check(id) != 0) bad |= 1u << 8;
    if (appslot_resume_source(id) != APP_RESUME_SRC_YIELD) bad |= 1u << 8;
    g_slot[id].parked_from_poll = 1;
    g_slot[id].parked_from_yield = 0;
    if (appslot_resume_source(id) != APP_RESUME_SRC_POLL) bad |= 1u << 8;
    g_slot[id].state = APP_STATE_WAIT_KEY;
    if (appslot_resume_source(id) != APP_RESUME_SRC_KBD) bad |= 1u << 8;
    g_slot[id].state = APP_STATE_PARKED;
    if (appslot_resume_source(id) != APP_RESUME_SRC_WAIT) bad |= 1u << 8;

    /* (9) STOP completion is the fifth origin; never supply a new EAX. */
    g_slot[id].state = APP_STATE_WAIT_POLL;
    g_slot[id].parked_from_poll = 0;
    g_slot[id].parked_from_yield = 0;
    g_slot[id].parked_from_stop = 1;
    if (appslot_resume_check(id) != 0 || appslot_kill_check(id) != 0 ||
        appslot_resume_source(id) != APP_RESUME_SRC_KEEP) bad |= 1u << 9;
    g_slot[id].parked_from_stop = 0;

    /* (7) tick の間引き (D8): 同じ tick では 2 度譲らない。間引きは表の検査
     * より**先**に効くので、弾き数 ring3_park_reject_count に載らない。
     * tick が進めば間引きは通り、表 (cur = シェル帯) の側で弾かれる。 */
    {
        u32 saved_reject = ring3_park_reject_count;
        u32 saved_tick   = g_poll_last_tick;
        u32 saved_yield  = ring3_poll_yield_count;

        g_poll_last_tick = 0x1234;
        if (appslot_park_poll_check(0x1234) != OS32_ERR_AGAIN) bad |= 1u << 7;
        if (ring3_park_reject_count != saved_reject) bad |= 1u << 7;
        if (appslot_park_poll_check(0x1235) != OS32_ERR_INVAL) bad |= 1u << 7;
        if (ring3_park_reject_count != saved_reject + 1) bad |= 1u << 7;
        /* 弾かれた試みも控えを進める = 同じ tick の連打は弾き数に載らない */
        if (appslot_park_poll_check(0x1235) != OS32_ERR_AGAIN) bad |= 1u << 7;
        if (ring3_park_reject_count != saved_reject + 1) bad |= 1u << 7;
        if (ring3_poll_yield_count != saved_yield) bad |= 1u << 7;

        ring3_park_reject_count = saved_reject;
        g_poll_last_tick = saved_tick;
    }

    /* 後始末: 借りたスロットも観測点も元に戻す (検査は 1 回も起こさない) */
    g_slot[id] = saved;
    g_cur = saved_cur;
    res_owner_set(saved_owner);
    ring3_resume_bad_frame_count = saved_badframe;
    if (ring3_switch_count != saved_switch) bad |= 1u << 5;
    return bad;
}

/* ======================================================================== */
/*  appslot_gfx_owner_selftest — 画面の所有者の遷移 (票 T8 D1 / D1a)         */
/*                                                                          */
/*  ブート時に踏むのは 2 つ:                                                 */
/*    (0) 遷移: 宣言のある CPL=3 アプリの gfx_init で所有者がその ID へ移り、 */
/*        回収 (appslot_gfx_owner_exit) で WM (1) へ戻る。CUI 中 (gui_mode   */
/*        = 0) は 1 のまま動かない。                                         */
/*    (1) 拒否: GUI 中に宣言の無い CPL=3 が呼んだら OS32_ERR_INVAL で、       */
/*        所有者は動かず gfx_init_reject_count だけが 1 増える。              */
/*                                                                          */
/*  空きスロット (APP_ID_MAX) を一時的に借りる。借りた中身・cur・owner・      */
/*  所有者・カウンタは丸ごと保存して戻す。                                   */
/* ======================================================================== */
/* ======================================================================== */
/*  GUI 中の CTRL+STOP は WM が宛先を決める (票 T9 §12 S6)                   */
/*                                                                          */
/*  壊れたときに実機で見えるのは「CTRL+STOP で関係ないアプリ (端末) まで     */
/*  消える」か「暴走したアプリを畳めない」だけで、原因が遠い。借りた         */
/*  スロットと cur / owner は必ず元へ戻す。ビット 0..n が落ちた項目。        */
/* ======================================================================== */
u32 appslot_abort_admit_selftest(void)
{
    u32 bad = 0;
    int id = APP_ID_MAX;
    AppSlot saved;
    int saved_cur = g_cur;
    int saved_owner = res_owner_get();

    saved = g_slot[id];
    slot_zero(&g_slot[id]);
    g_slot[id].state = APP_STATE_RUNNING;
    g_slot[id].cpl3 = 1;
    g_slot[id].last_kernel_tick = 1000;
    g_cur = id;

    /* (0) CUI 中は従来どおり立てる (K2 の逃げ道を 1 バイトも変えない) */
    if (appslot_abort_admit(0, 1000) != 1) bad |= 1u << 0;
    if (appslot_abort_admit(0, 1000 + APP_RUNAWAY_TICKS) != 1) bad |= 1u << 0;

    /* (1) GUI 中にアプリのコードを割り込んだときは立てない
     * — 宛先は WM が launch_child で解決する (D8) */
    if (appslot_abort_admit(1, 1000) != 0) bad |= 1u << 1;
    if (appslot_abort_admit(1, 1000 + APP_RUNAWAY_TICKS - 1) != 0) bad |= 1u << 1;

    /* (4) ただし gui_call(OP_WAIT) の中なら立てる (K5c の経路)。
     * 割り込まれた文脈は CPL=0 (WM) なので、要求は必ず WM のハンドラが見る。 */
    g_slot[id].in_op_wait = 1;
    if (appslot_abort_admit(1, 1000) != 1) bad |= 1u << 4;
    if (appslot_abort_admit(0, 1000) != 1) bad |= 1u << 4;   /* CUI も従来どおり */
    g_slot[id].in_op_wait = 0;
    if (appslot_abort_admit(1, 1000) != 0) bad |= 1u << 4;

    /* (2) 暴走だけは GUI 中でも立てる = 最後に **カーネルへ入って** から
     * APP_RUNAWAY_TICKS 以上 (tick が一周しても差で見る)。op_wait で待って
     * いるアプリは syscall の入口で控えが進むので、ここには掛からない。 */
    if (appslot_abort_admit(1, 1000 + APP_RUNAWAY_TICKS) != 1) bad |= 1u << 2;
    g_slot[id].last_kernel_tick = 0xFFFFFF00UL;
    if (appslot_abort_admit(1, 0xFFFFFF00UL + APP_RUNAWAY_TICKS) != 1) {
        bad |= 1u << 2;
    }
    if (appslot_abort_admit(1, 0xFFFFFF00UL + 1u) != 0) bad |= 1u << 2;

    /* (3) WM (シェル帯) が走っているときは GUI 中も CUI 中も立てない
     * — CUI では appslot_abort_request 側が弾く (従来どおり)。 */
    g_cur = APP_ID_SHELL;
    if (appslot_abort_admit(1, 0) != 0) bad |= 1u << 3;
    if (appslot_abort_request() != 0) bad |= 1u << 3;

    g_slot[id] = saved;
    g_cur = saved_cur;
    res_owner_set(saved_owner);
    return bad;
}

u32 appslot_gfx_owner_selftest(void)
{
    u32 bad = 0;
    int id = APP_ID_MAX;
    AppSlot saved;
    int saved_cur = g_cur;
    int saved_owner = res_owner_get();
    int saved_gfx = g_gfx_owner;
    u32 saved_reject = gfx_init_reject_count;

    saved = g_slot[id];
    slot_zero(&g_slot[id]);
    g_slot[id].state = APP_STATE_RUNNING;
    g_slot[id].cpl3 = 1;
    g_slot[id].gui = 1;
    g_cur = id;
    res_owner_set(id);
    g_gfx_owner = GFX_OWNER_WM;

    /* (0) 遷移: 宣言ありなら取り、回収で WM へ戻る。CUI 中は動かない。 */
    g_slot[id].hdr_flags = OS32X_FLAG_GFX;
    if (appslot_gfx_claim(0) != 0) bad |= 1u << 0;
    if (g_gfx_owner != GFX_OWNER_WM) bad |= 1u << 0;   /* CUI は触らない */
    if (appslot_gfx_claim(1) != 0) bad |= 1u << 0;
    if (g_gfx_owner != id) bad |= 1u << 0;
    appslot_gfx_owner_exit(GFX_OWNER_WM);              /* 他人の回収では戻らない */
    if (g_gfx_owner != id) bad |= 1u << 0;
    appslot_gfx_owner_exit(id);
    if (g_gfx_owner != GFX_OWNER_WM) bad |= 1u << 0;

    /* (1) 拒否: GUI 中の宣言なしは ERR_INVAL。所有者は動かず、数だけ増える。 */
    g_slot[id].hdr_flags = 0;
    if (appslot_gfx_claim(1) != OS32_ERR_INVAL) bad |= 1u << 1;
    if (g_gfx_owner != GFX_OWNER_WM) bad |= 1u << 1;
    if (gfx_init_reject_count != saved_reject + 1) bad |= 1u << 1;
    /* 票 T8-2: 拒否したアプリは syscall 出口で畳まれる (abort_req が立つ)。 */
    if (!g_slot[id].abort_req) bad |= 1u << 1;
    g_slot[id].abort_req = 0;
    if (appslot_gfx_claim(0) != 0) bad |= 1u << 1;     /* CUI 中は通す */
    if (gfx_init_reject_count != saved_reject + 1) bad |= 1u << 1;
    if (g_slot[id].abort_req) bad |= 1u << 1;          /* 素通しは畳まない */

    /* (2) CUI 専用の宣言 (票 T8-2): GUI からの起動だけを断つ。 */
    if (appslot_cui_only_admit(1, OS32X_FLAG_CUI_ONLY) != OS32_ERR_INVAL)
        bad |= 1u << 2;
    if (appslot_cui_only_admit(0, OS32X_FLAG_CUI_ONLY) != 0) bad |= 1u << 2;
    if (appslot_cui_only_admit(1, 0) != 0) bad |= 1u << 2;

    /* 後始末 */
    g_slot[id] = saved;
    g_cur = saved_cur;
    res_owner_set(saved_owner);
    g_gfx_owner = saved_gfx;
    gfx_init_reject_count = saved_reject;
    return bad;
}
