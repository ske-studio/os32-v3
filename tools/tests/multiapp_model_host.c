/* ========================================================================
 *  multiapp_model_host.c — K5a のホスト状態モデル (GUI アプリ 4 本同時)
 *
 *  対象票: docs/archive/gui_v13/TASK_K5_multiapp.md §K5a-8
 *  実行:   python3 -B tools/tests/test_multiapp_model.py
 *
 *  ここに置いてあるのは「カーネルの状態遷移だけを抜き出した純粋な模型」で、
 *  カーネルのソースは 1 行も #include しない (K5a は読取専用の段階で、
 *  kernel/ / exec/ を変えていないため実ソースを噛ませても何も試験できない)。
 *  K5b で kernel/multiapp.c を書くときは、この模型の遷移表がそのまま仕様に
 *  なる — 名前と規則を写して、実体 (addrspace / setjmp / pgalloc) を足す。
 *
 *  模型が持つ 6 つの規則 (票 §K5a の 3/4/5/6 に対応):
 *    R1 ID は 1 = シェル帯、2..5 = アプリ。同時に生きられる非シェル ID は 4 本、
 *       5 本目は ERR_FULL。GUI アプリでも CUI の入れ子 exec でも同じ 1 つの池。
 *    R2 走っているのは常に 1 本。park (OP_WAIT の中) → WM 復帰 → resume で
 *       別の 1 本、の往復しか無い。プリエンプションは無い。
 *    R3 park できるのは「いま走っているアプリが gui_call(OP_WAIT) の中に居る」
 *       ときだけ。他の op の中や top-level からは ERR_STATE。
 *    R4 終了 (exit / fault / kill) はその ID の資源だけを回収する。
 *    R5 GUI アプリの起動は WM の top-level からだけ (契約 S2)。CUI の入れ子
 *       exec_run は従来どおり走っているアプリからも通る。
 *    R6 同時に ready が複数居るときの選択は決定的で、かつ**有界** (票 D11):
 *       turn は 1 ラウンドにつきアプリごと 1 回、入力優先の据え置きは連続
 *       MA_INPUT_STREAK_MAX 回まで。ready なアプリは park した OP_WAIT から
 *       MA_STARVE_BOUND 回の OP_WAIT 以内に走る (ラウンドをまたぐ待ちを含む)。
 *       鍵待ち (MA_WAIT_KEY) は注入リングに未読があるときだけ ready で、
       入力群として扱う (票 K7 D3)。
       順は 入力群 > 導出群、入力群の中はフォーカス優先、
 *       それ以外は last_run の次から ID 昇順の巡回。
 *       ただし top-level にしか出来ない仕事 (LAUNCH の保留 = launch_pending)
 *       があるときは、他の条件より先に譲る (票 D11-3、PM 受入 2026-09-11)。
 *
 *  C89 ([C1])。libc も OS32 のヘッダも使わない (-nostdlib で直接走る)。
 * ======================================================================== */

typedef unsigned int   u32;

#define NOINST __attribute__((no_instrument_function))

/* ------------------------------------------------------------------ */
/*  ここから下が「将来 kernel/multiapp.c になる」部分                    */
/* ------------------------------------------------------------------ */

#define MA_MAX_APPS   4              /* 同時に生かせる非シェル ID の本数 */
#define MA_SHELL_ID   1              /* = kernel/gui.h の GUI_SHELL_OWNER */
#define MA_ID_MIN     2
#define MA_ID_MAX     (MA_ID_MIN + MA_MAX_APPS - 1)   /* 5 */
#define MA_SLOT_MAX   4              /* = include/memmap.h の GUI_SLOT_MAX */

/* 資源の種類。実体は fd_redirect / vfs / pipe / gui 窓 / gui タイマ / shm。
 * 模型では「所有者タグ付きの数」だけを持つ (回収が ID 単位で閉じるかを見る)。 */
#define MA_RES_FD     0
#define MA_RES_PIPE   1
#define MA_RES_WINDOW 2
#define MA_RES_TIMER  3
#define MA_RES_SHM    4
#define MA_RES_KINDS  5

/* 戻り値。符号は OS32_ERR_* の流儀 (負がエラー) に合わせる。 */
#define MA_OK          0
#define MA_ERR_INVAL  (-1)
#define MA_ERR_NOMEM  (-2)
#define MA_ERR_FULL   (-3)
#define MA_ERR_STATE  (-4)
/* 注入リングが空のまま鍵待ちを起こそうとした (= OS32_ERR_AGAIN、票 K7 指摘 B)。 */
#define MA_ERR_AGAIN  (-5)

/* アプリの状態 */
#define MA_FREE     0
#define MA_RUNNING  1
#define MA_PARKED   2
/* 第 2 の park 点 = kbd 待ち (票 K7 D1)。exec/appslot.h の APP_STATE_WAIT_KEY
 * と同値で、exec_app_state もこの 3 を返す (既存の 0/1/2 の意味は動かない)。 */
#define MA_WAIT_KEY 3
/* 第 3 の park 点 = ポーリング型の協調 yield (票 T8 §7 D8)。GUI 中に
 * kbd_trygetchar / kbd_trygetkey / kbd_has_key を回す全画面 GFX プログラムを
 * 1 周だけ WM へ譲らせる印。exec/appslot.h の APP_STATE_WAIT_POLL と同値で、
 * 足すのは別票 T8-3 K (既存の 0〜3 の意味は動かない)。 */
#define MA_WAIT_POLL 4

/* D11 (2026-09-10 の差し戻し): 入力優先を**連続で**適用してよい OP_WAIT の回数。
 * これを超えたら、自分に入力が湧き続けていても譲る。入力の源が人間とは限らない
 * (アプリが自分の 2 窓へ交互に set_focus すると Focus が自分に湧き続ける:
 *  userland/gshell/src/wm.rs:969-985 → input.rs:855-872) ので、上限が要る。
 * 値を MA_MAX_APPS と同じにしたのは「アプリの数だけは続けて持てる」という
 * 説明できる線を引くため (それ以上の意味は無い)。 */
#define MA_INPUT_STREAK_MAX  MA_MAX_APPS

/* ready なアプリが「自分が park した OP_WAIT」から再開までに待つ最悪回数。
 * 独立レビュー 2026-09-10 [P2] (2 回目) で再導出した (旧: MA_MAX_APPS × ...)。
 *
 *   1 turn                     ≤ MA_INPUT_STREAK_MAX + 1 回の OP_WAIT
 *                                (据え置き上限 + park する 1 回)
 *   現ラウンドの残り           ≤ N - 1 turn   (自分の turn_used は立っている)
 *   次ラウンドで自分より先     ≤ N - 1 turn   (turn_used が 2 回目を止める)
 *   ⇒ (2N - 2) × (STREAK_MAX + 1)
 *
 * 前提: 起算点はそのアプリが park した OP_WAIT、その後ずっと ready、
 * その間アプリ集合が変わらない (起動・終了・kill が無い)。N = 生きている
 * アプリ数で、定数は最大値 MA_MAX_APPS = 4 で取る ⇒ 6 × 5 = 30。
 * ケース 15 (N=2 → 2×5=10) と ケース 16 (N=4 → 30) はどちらもこの式に
 * ちょうど届く = 上界であると同時にタイト。 */
#define MA_STARVE_BOUND \
    ((2 * MA_MAX_APPS - 2) * (MA_INPUT_STREAK_MAX + 1))

/* gui_call の op。模型が区別するのは「OP_WAIT かどうか」だけ。 */
#define MA_OP_WAIT   1
#define MA_OP_POLL   2
#define MA_OP_COMMIT 3

typedef struct {
    int  state;
    int  slot;                 /* 0..3。CUI の入れ子子には配らない (-1) */
    u32  pages;                /* この ID が握っている物理ページ数 */
    int  res[MA_RES_KINDS];
    int  parent;               /* 起動した側の ID (GUI アプリは常に 1) */
    int  in_op_wait;           /* いま gui_call(OP_WAIT) の中に居るか */
    int  abort_req;            /* CTRL+STOP 要求 (この ID 宛) */
    int  gui;                  /* GUI アプリ (スロットを持つ) か */
    int  input_ready;          /* 未読の待ち行列型 / sticky Quit がある (D11) */
    int  derived_ready;        /* 期限切れ Timer / Configure / 配送できる Paint */
    int  turn_used;            /* このラウンドで turn を 1 回使った (D11 の有界性) */
} MaApp;

typedef struct {
    MaApp app[MA_MAX_APPS];    /* 添字 = id - MA_ID_MIN */
    int   cur;                 /* 走っている ID。1 = WM top-level / シェル */
    int   owner;               /* res_owner_get() が返す値 */
    u32   free_pages;
    u32   pd_switches;         /* アプリ PD を CR3 に載せた回数 */
    u32   reclaims;            /* 回収 (ma_reclaim) を回した回数 */
    int   last_reclaim_id;
    int   exited_id;           /* 直前に畳んだ ID (gui_owner_exit の引数) */
    int   exit_status;
    int   last_run;            /* 直前に走った ID (D11 の巡回の起点)。0 = 無し */
    int   focus;               /* 最前面窓の owner (gshell の front_owner()) */
    int   input_streak;        /* 入力優先で turn を据え置いた連続 OP_WAIT 回数 */
    int   launch_pending;      /* top-level でしか出来ない起動要求が保留中 (D11-3) */
    u32   kbd_pending;         /* 注入リング (kbd_inject) の未読バイト数 (K7 D2/D3) */
} MaState;

static void ma_zero(void *p, u32 n) NOINST;
static void ma_zero(void *p, u32 n)
{
    unsigned char *b = (unsigned char *)p;
    u32 i;
    for (i = 0; i < n; i++) b[i] = 0;
}

static void ma_init(MaState *st, u32 free_pages) NOINST;
static void ma_init(MaState *st, u32 free_pages)
{
    int i;
    ma_zero(st, (u32)sizeof(*st));
    for (i = 0; i < MA_MAX_APPS; i++) {
        st->app[i].state = MA_FREE;
        st->app[i].slot = -1;
    }
    st->cur = MA_SHELL_ID;
    st->owner = MA_SHELL_ID;
    st->free_pages = free_pages;
}

static MaApp *ma_app(MaState *st, int id) NOINST;
static MaApp *ma_app(MaState *st, int id)
{
    if (id < MA_ID_MIN || id > MA_ID_MAX) return (MaApp *)0;
    if (st->app[id - MA_ID_MIN].state == MA_FREE) return (MaApp *)0;
    return &st->app[id - MA_ID_MIN];
}

/* 空き ID は必ず**小さい方から**。CUI の入れ子 exec で 2,3,4,5 と並び、
 * 「段 = ID」の従来の見え方がそのまま残るための決め (票 §K5a-3)。 */
static int ma_alloc_id(MaState *st) NOINST;
static int ma_alloc_id(MaState *st)
{
    int i;
    for (i = 0; i < MA_MAX_APPS; i++) {
        if (st->app[i].state == MA_FREE) return MA_ID_MIN + i;
    }
    return MA_ERR_FULL;
}

/* SHM スロットも小さい方から。gshell の alloc_slot (wm.rs:641) と同じ規則で、
 * ID とスロットは 1 対 1 (契約 T2a: スロットは所有者に紐づく)。 */
static int ma_alloc_slot(MaState *st) NOINST;
static int ma_alloc_slot(MaState *st)
{
    int s, i, taken;
    for (s = 0; s < MA_SLOT_MAX; s++) {
        taken = 0;
        for (i = 0; i < MA_MAX_APPS; i++) {
            if (st->app[i].state != MA_FREE && st->app[i].slot == s) taken = 1;
        }
        if (!taken) return s;
    }
    return MA_ERR_FULL;
}

/* ------------------------------------------------------------------ */
/*  起動 — 塞がない (exec_start)。ID の池は GUI と CUI で 1 つ。        */
/* ------------------------------------------------------------------ */
static int ma_start(MaState *st, u32 pages, int gui) NOINST;
static int ma_start(MaState *st, u32 pages, int gui)
{
    int id, slot = -1;
    MaApp *a;

    /* GUI アプリの起動は WM の top-level からだけ (契約 V12-S の S2:
     * gui_call ハンドラ / X3 / X4 から起動を呼んではならない)。
     * CUI の入れ子 exec_run (gui=0) は従来どおり走っているアプリからも通す。 */
    if (gui && st->cur != MA_SHELL_ID) return MA_ERR_STATE;

    id = ma_alloc_id(st);
    if (id < 0) return MA_ERR_FULL;                   /* 5 本目 (票 G3) */
    if (pages > st->free_pages) return MA_ERR_NOMEM;  /* 拒否。切り詰めない */
    if (gui) {
        slot = ma_alloc_slot(st);
        if (slot < 0) return MA_ERR_FULL;             /* SHM スロット満杯 */
    }

    a = &st->app[id - MA_ID_MIN];
    ma_zero(a, (u32)sizeof(*a));
    a->state = MA_RUNNING;
    a->slot = slot;
    a->pages = pages;
    a->parent = st->cur;
    a->gui = gui;
    st->free_pages -= pages;

    /* ここで iret = アプリ PD を CR3 に載せる (票 §K5a-2)。 */
    a->turn_used = 1;              /* このラウンドの turn を使った */
    st->cur = id;
    st->owner = id;
    st->last_run = id;
    st->input_streak = 0;
    st->pd_switches++;
    return id;
}

static int ma_live(const MaState *st) NOINST;
static int ma_live(const MaState *st)
{
    int i, n = 0;
    for (i = 0; i < MA_MAX_APPS; i++) if (st->app[i].state != MA_FREE) n++;
    return n;
}

/* ------------------------------------------------------------------ */
/*  D11: 同時 ready の選択規則                                          */
/*                                                                     */
/*  起床の理由は 2 群に分ける (契約 T3):                                */
/*    入力群 input_ready  = 未読の待ち行列型 / sticky Quit (契約 S5)     */
/*    導出群 derived_ready= 期限切れ Timer / Configure 未通知 /          */
/*                          配送できる Paint / OP_WAIT の timeout        */
/*  実物ではどちらも WM が毎周期そのつど計算する (handler.rs の          */
/*  wake_ready + session::quit[])。模型では試験が直接立てる。            */
/* ------------------------------------------------------------------ */
static void ma_set_ready(MaState *st, int id, int input, int derived) NOINST;
static void ma_set_ready(MaState *st, int id, int input, int derived)
{
    MaApp *a = ma_app(st, id);
    if (!a) return;
    a->input_ready = input;
    a->derived_ready = derived;
}

/* 鍵待ち (票 K7 D3): 止まっている理由が kbd なら、起床の理由は
 * 「注入リングに未読がある」ことだけ。スロットを持たないので
 * input_ready / derived_ready はどちらも立たない。 */
static int ma_key_ready(const MaState *st, const MaApp *a) NOINST;
static int ma_key_ready(const MaState *st, const MaApp *a)
{
    return a->state == MA_WAIT_KEY && st->kbd_pending > 0;
}

/* Product correspondence (userland/gshell/src/multiapp.rs):
 * ma_input_group -> input_ready (ring/sticky Quit or WAIT_KEY injection).
 * ma_ready -> ready + round_remaining/pick_group's runnable-app filter.
 * WAIT_POLL with input/derived joins the round; only EMPTY WAIT_POLL falls
 * through to ma_pick_poll. State x input x derived is checked in
 * case_poll_gui_ready and tools/tests/g3fix_wm_probe.rs.
 * The model stores readiness bits; the product derives them from WM state. */
static int ma_input_group(const MaState *st, const MaApp *a) NOINST;
static int ma_input_group(const MaState *st, const MaApp *a)
{
    if (a->state == MA_WAIT_KEY) return ma_key_ready(st, a);
    return (a->state == MA_PARKED || a->state == MA_WAIT_POLL) && a->input_ready;
}

/* 起こせるか (止まっていて、起床の理由がある)。 */
static int ma_ready(const MaState *st, const MaApp *a) NOINST;
static int ma_ready(const MaState *st, const MaApp *a)
{
    if (a->state == MA_WAIT_KEY) return ma_key_ready(st, a);
    if (a->state != MA_PARKED && a->state != MA_WAIT_POLL) return 0;
    return a->input_ready || a->derived_ready;
}

/* 止まっていて、この turn を使っていない ready を数える (ラウンドの残り)。 */
static int ma_round_remaining(const MaState *st) NOINST;
static int ma_round_remaining(const MaState *st)
{
    int i, n = 0;
    for (i = 0; i < MA_MAX_APPS; i++) {
        const MaApp *a = &st->app[i];
        if (!a->turn_used && ma_ready(st, a)) n++;
    }
    return n;
}

/* 群の中を last_run の次から ID 昇順に巡り、最初の 1 本を返す (0 = 無し)。
 * 候補は「このラウンドで turn を使っていない」park 中のアプリだけ。
 * turn を 1 ラウンド 1 回に絞るのが飢餓を止める仕掛けで、巡回はその中の
 * 順を決定的にするためのもの。種別で全順序をつけないのは、Paint しか無い
 * アプリが repeat タイマ持ちに永久に負けるのを避けるため。 */
static int ma_pick_group(const MaState *st, int want_input) NOINST;
static int ma_pick_group(const MaState *st, int want_input)
{
    int start, n, i;
    start = (st->last_run >= MA_ID_MIN && st->last_run <= MA_ID_MAX) ?
            (st->last_run - MA_ID_MIN + 1) : 0;
    for (n = 0; n < MA_MAX_APPS; n++) {
        const MaApp *a;
        i = (start + n) % MA_MAX_APPS;
        a = &st->app[i];
        if (a->turn_used || !ma_ready(st, a)) continue;
        if (want_input) {
            if (ma_input_group(st, a)) return MA_ID_MIN + i;
        } else {
            if (!ma_input_group(st, a)) return MA_ID_MIN + i;
        }
    }
    return 0;
}

/* ポーリング群 (票 T8 §7 D8) — **最下位**の 1 本 (0 = 無し)。
 * WAIT_POLL は「常に ready」だが、ここを呼ぶのは入力群も導出群も空の周だけ
 * (ma_pick の中の 1 か所)。last_run の巡回には乗せず **ID 昇順**で選ぶ:
 * ラウンドの turn を数えないので巡回の公平さは要らず、決定的な順だけが要る。
 * top-level にしか出来ない仕事 (LAUNCH の保留) がある周は WM の番なので譲らない。 */
static int ma_pick_poll(const MaState *st) NOINST;
static int ma_pick_poll(const MaState *st)
{
    int i;
    if (st->launch_pending) return 0;
    for (i = 0; i < MA_MAX_APPS; i++) {
        if (st->app[i].state == MA_WAIT_POLL) return MA_ID_MIN + i;
    }
    return 0;
}

/* 自分 (st->cur) 以外にポーリングで譲った 1 本が居るか (票 T8 §7 D8)。
 * ma_ready とは別に持つ: 順を決めるのは ma_pick (poll は最下位) で、
 * こちらが決めるのは「走っているアプリが top-level へ戻る道を開けるか」だけ。 */
static int ma_poll_live(const MaState *st) NOINST;
static int ma_poll_live(const MaState *st)
{
    int i;
    for (i = 0; i < MA_MAX_APPS; i++) {
        if (st->cur != MA_ID_MIN + i && st->app[i].state == MA_WAIT_POLL) return 1;
    }
    return 0;
}

/* 次に起こす 1 本 (0 = 誰も起こさない)。WM top-level が使う。
 * ラウンドの turn が尽きたら全員ぶんを配り直す (= 新しいラウンド)。 */
static int ma_pick(MaState *st) NOINST;
static int ma_pick(MaState *st)
{
    int f, k, i;
    if (st->cur != MA_SHELL_ID) return 0;   /* 走っている間は選ばない */

    if (ma_round_remaining(st) == 0) {
        for (i = 0; i < MA_MAX_APPS; i++) st->app[i].turn_used = 0;
        /* ready が 1 本も無い周。ここだけがポーリング群へ降りる口 (D8 の
         * 「最下位」) で、ラウンドにも上界 (MA_STARVE_BOUND) にも数えない。 */
        if (ma_round_remaining(st) == 0) return ma_pick_poll(st);
    }

    /* (1) 入力群にフォーカス窓の owner が居れば、それを最優先。
     *     turn を使い切っていれば飛ばす — フォーカスを握ったままのアプリが
     *     ラウンドを独占できないようにする (差し戻しの反例)。 */
    f = st->focus;
    if (f >= MA_ID_MIN && f <= MA_ID_MAX) {
        const MaApp *a = &st->app[f - MA_ID_MIN];
        if (!a->turn_used && ma_input_group(st, a)) return f;
    }
    /* (2) 入力群を巡回 → (3) 空なら導出群を巡回 */
    k = ma_pick_group(st, 1);
    if (k) return k;
    return ma_pick_group(st, 0);
}

/* 走っているアプリが OP_WAIT の中で park すべきか。
 *   - top-level にしか出来ない仕事が保留     → 譲る (下記)
 *   - 他に ready が 1 本も無い          → 戻る (1 本のときの回帰ゼロ)
 *   - 自分に入力があり、据え置きが上限未満 → 戻る (打鍵の連続を取りこぼさない)
 *   - それ以外                          → 譲る
 * 3 行目に上限を置いたのが差し戻しの修正点。入力の源は人間とは限らず
 * (自分の 2 窓へ交互に set_focus すれば自分で湧かせられる)、上限が無いと
 * 他のアプリの turn が永久に回ってこない。
 *
 * 1 行目は PM 受入 2026-09-11 (K5b-W からの提案) で足した分岐。GUI アプリの
 * 起動は WM の top-level からしか通らず (R5 / 契約 S2)、top-level へ戻る道は
 * park だけなので、これが無いとアプリが 1 本走っている間は 2 本目が永久に
 * 立たない (2 行目に当たって譲らない)。起動要求は有限個の事象で、消費されれば
 * 条件も消えるため D11-3a の上界は変わらない (ケース 17g / 17h)。
 * 実装側は userland/gshell/src/multiapp.rs の should_park の
 * `st.launch_pending || has_top_level_work(mm)`。 */
static int ma_should_park(MaState *st) NOINST;
static int ma_should_park(MaState *st)
{
    MaApp *a;
    int i, other_ready = 0;
    if (st->cur < MA_ID_MIN || st->cur > MA_ID_MAX) return 0;
    a = &st->app[st->cur - MA_ID_MIN];
    if (a->state != MA_RUNNING) return 0;
    /* (a) top-level にしか出来ない仕事 (LAUNCH の保留)。据え置きより先に見る。 */
    if (st->launch_pending) return 1;
    for (i = 0; i < MA_MAX_APPS; i++) {
        /* 鍵待ちも「起こせる 1 本」として数える (票 K7 D3)。自分は
         * MA_RUNNING なので ma_ready が 0 を返す = 数に入らない。 */
        if (ma_ready(st, &st->app[i])) other_ready = 1;
    }
    /* 票 T8 §7 D8 (PM 実測 2026-09-12、受入 F8 不合格の修正): ready が 1 本も
     * 無くても、ポーリングで譲った 1 本 (MA_WAIT_POLL) が居るなら譲る。
     * 実物では走っているアプリが park しない限り op_wait の中を
     * 「wm_cycle + sys_halt」で回るだけで、ma_pick_poll を呼ぶ点
     * (= WM top-level) へ行けない = ポーリングの 1 本が永久に起きない。
     * **「最下位」は ma_pick の側で決まる**ので、ここに足しても起こす順は
     * 変わらない (変わるのは top-level へ戻る道が開くかどうかだけ)。
     * 譲りが増えるだけなので MA_STARVE_BOUND は伸びない。 */
    if (!other_ready && !ma_poll_live(st)) return 0;
    if (a->input_ready && st->input_streak < MA_INPUT_STREAK_MAX) {
        st->input_streak++;
        return 0;
    }
    return 1;
}

/* gui_call の入口。模型が区別するのは「OP_WAIT かどうか」だけ。 */
static int ma_gui_call(MaState *st, int op) NOINST;
static int ma_gui_call(MaState *st, int op)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    a->in_op_wait = (op == MA_OP_WAIT) ? 1 : 0;
    return MA_OK;
}

/* gui_call から出る (WM のハンドラが戻り、アプリのコードへ返る)。 */
static int ma_gui_return(MaState *st) NOINST;
static int ma_gui_return(MaState *st)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    a->in_op_wait = 0;
    return MA_OK;
}

/* ------------------------------------------------------------------ */
/*  park — 走っているアプリを OP_WAIT の中で止め、WM top-level へ戻す   */
/*  契約 T2a「譲り合いの点は OP_WAIT だけ」の実装点。                    */
/* ------------------------------------------------------------------ */
static int ma_park(MaState *st) NOINST;
static int ma_park(MaState *st)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    if (!a->in_op_wait) return MA_ERR_STATE;   /* OP_WAIT 以外では切替えない */
    a->state = MA_PARKED;
    st->cur = MA_SHELL_ID;
    st->owner = MA_SHELL_ID;
    return MA_OK;
}

/* ------------------------------------------------------------------ */
/*  park (第 2 の点) — GUI 中の kbd_getchar で cooked が空 (票 K7 D1)   */
/*                                                                     */
/*  OP_WAIT の中である必要は無く、スロットを持たない CUI の入れ子の子   */
/*  (gui = 0) でも通る。注入リングに文字が残っているなら park しない    */
/*  (指摘 R1: 続きバイトをそのまま返す)。                               */
/* ------------------------------------------------------------------ */
static int ma_park_kbd(MaState *st) NOINST;
static int ma_park_kbd(MaState *st)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    if (st->kbd_pending > 0) return MA_ERR_STATE;   /* 文字があるなら止めない */
    a->state = MA_WAIT_KEY;
    st->cur = MA_SHELL_ID;
    st->owner = MA_SHELL_ID;
    return MA_OK;
}

/* ------------------------------------------------------------------ */
/*  park (第 3 の点) — ポーリング型の協調 yield (票 T8 §7 D8)          */
/*                                                                     */
/*  kbd_trygetchar が「キー無し」を返す前に 1 周だけ WM へ譲る。GUI 中 / */
/*  注入リングが空 / 前回の譲りから tick が進んだ、の 3 条件はカーネル   */
/*  側 (別票 T8-3 K) の判断で、模型が写すのは「リングに文字があるなら    */
/*  譲らない」だけ。OP_WAIT の中である必要は無い。                       */
/* ------------------------------------------------------------------ */
static int ma_park_poll(MaState *st) NOINST;
static int ma_park_poll(MaState *st)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    if (st->kbd_pending > 0) return MA_ERR_STATE;   /* 文字があるなら止めない */
    a->state = MA_WAIT_POLL;
    st->cur = MA_SHELL_ID;
    st->owner = MA_SHELL_ID;
    return MA_OK;
}

/* ------------------------------------------------------------------ */
/*  resume — 止めてあるアプリを 1 本だけ起こす (GetMessage 方式)         */
/*  WM top-level からしか呼べない = 走っているアプリの横取りは無い。     */
/* ------------------------------------------------------------------ */
static int ma_resume(MaState *st, int id) NOINST;
static int ma_resume(MaState *st, int id)
{
    MaApp *a;
    if (st->cur != MA_SHELL_ID) return MA_ERR_STATE;
    a = ma_app(st, id);
    if (!a) return MA_ERR_INVAL;
    if (a->state == MA_WAIT_KEY) {
        /* 指摘 B: 文字の取り出しはカーネル側で完結する。空なら印 (WAIT_KEY)
         * を残したまま AGAIN で拒み、WM にその周を譲らせる。 */
        if (st->kbd_pending == 0) return MA_ERR_AGAIN;
        st->kbd_pending--;
    } else if (a->state == MA_WAIT_POLL) {
        /* 票 T8 §7 D8: 印を見てカーネルが EAX を上書きする — 文字があれば
         * 1 バイト、無ければ -1 (キー無し)。**空でも拒まない** (AGAIN は
         * 鍵待ちだけ) ので、ポーリングは必ず 1 周で戻れる。 */
        if (st->kbd_pending > 0) st->kbd_pending--;
    } else if (a->state != MA_PARKED) {
        return MA_ERR_STATE;
    }
    a->state = MA_RUNNING;
    a->in_op_wait = 0;   /* OP_WAIT はここで戻る (戻り値は WM が決める) */
    a->turn_used = 1;              /* このラウンドの turn を使った */
    st->cur = id;
    st->owner = id;
    st->last_run = id;
    st->input_streak = 0;
    st->pd_switches++;
    return MA_OK;
}
/* 資源の登録 (vfs_open / pipe_alloc / create_window / timer / shm_alloc 相当)。
 * 実物はどれも res_owner_get() でタグを打つ (fs/vfs_fd.c:139 ほか)。 */
static int ma_res_add(MaState *st, int kind, int n) NOINST;
static int ma_res_add(MaState *st, int kind, int n)
{
    MaApp *a;
    if (kind < 0 || kind >= MA_RES_KINDS) return MA_ERR_INVAL;
    a = ma_app(st, st->owner);
    if (!a) return MA_ERR_STATE;   /* シェル帯の資源は模型の対象外 */
    a->res[kind] += n;
    return MA_OK;
}

/* ------------------------------------------------------------------ */
/*  回収 — この ID の資源だけを返す (票 R4 / 契約 T4・U8)               */
/*  実物の並び: fd_redirect_reset_owned / vfs_close_owned /             */
/*  pipe_free_owned / shm_free_owned / db_cleanup_owned /               */
/*  gui_owner_exit(id) — 全部この 1 つの ID で呼ぶ。                    */
/* ------------------------------------------------------------------ */
static void ma_reclaim(MaState *st, int id) NOINST;
static void ma_reclaim(MaState *st, int id)
{
    MaApp *a = &st->app[id - MA_ID_MIN];
    u32 pages = a->pages;
    ma_zero(a, (u32)sizeof(*a));
    a->state = MA_FREE;
    a->slot = -1;
    st->free_pages += pages;
    st->reclaims++;
    st->last_reclaim_id = id;
    st->exited_id = id;        /* gui_owner_exit(id) はこの ID で呼ばれる */
}

static void ma_to_toplevel(MaState *st) NOINST;
static void ma_to_toplevel(MaState *st)
{
    st->cur = MA_SHELL_ID;
    st->owner = MA_SHELL_ID;
}

/* 正常終了 (sys_exit)。畳めるのは「いま走っている 1 本」だけ。
 * 親が生きているアプリ (= CUI の入れ子 exec_run) ならその段へ戻り、
 * そうでなければ WM top-level (ID 1) へ戻る。 */
static int ma_exit(MaState *st, int status) NOINST;
static int ma_exit(MaState *st, int status)
{
    MaApp *a = ma_app(st, st->cur);
    int parent;
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    parent = a->parent;
    st->exit_status = status;
    ma_reclaim(st, st->cur);
    if (parent >= MA_ID_MIN && ma_app(st, parent) != 0) {
        st->cur = parent;
        st->owner = parent;
    } else {
        ma_to_toplevel(st);
    }
    return MA_OK;
}
/* fault (#PF / #GP / 範囲外 slot) — ring3_fault_kill の模型。
 * 畳む対象は「いま走っている 1 本」だけで、後始末の道筋は正常終了と同じ。 */
static int ma_fault(MaState *st) NOINST;
static int ma_fault(MaState *st)
{
    return ma_exit(st, -1);
}
/* CTRL+STOP (契約 T6)。IRQ1 は「いま走っているアプリ」しか知らないので、
 * 要求はその 1 本にしか立たない。止めてあるアプリには届かない。 */
static int ma_abort_request(MaState *st) NOINST;
static int ma_abort_request(MaState *st)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    a->abort_req = 1;
    return MA_OK;
}

/* 次の安全地点 (syscall 入口 / 出口 / IRQ1 スタブ) で畳む。 */
static int ma_abort_check(MaState *st) NOINST;
static int ma_abort_check(MaState *st)
{
    MaApp *a = ma_app(st, st->cur);
    if (!a || a->state != MA_RUNNING) return MA_ERR_STATE;
    if (!a->abort_req) return MA_OK;
    return ma_exit(st, -2);
}

/* WM からの kill。止めてあるアプリを**起こさずに**畳む (top-level 専用)。
 * これが無いと、resume されないアプリは二度と畳めない。 */
static int ma_kill(MaState *st, int id) NOINST;
static int ma_kill(MaState *st, int id)
{
    MaApp *a;
    if (st->cur != MA_SHELL_ID) return MA_ERR_STATE;
    a = ma_app(st, id);
    if (!a) return MA_ERR_INVAL;
    /* 指摘 D (D5): 鍵待ちのまま畳める (exec/appslot.c の resume/kill と同じ線)。 */
    if (a->state != MA_PARKED && a->state != MA_WAIT_KEY) return MA_ERR_STATE;
    ma_reclaim(st, id);
    return MA_OK;
}

/* ------------------------------------------------------------------ */
/*  ここから下は試験ハーネス (カーネルへは移さない)                     */
/* ------------------------------------------------------------------ */

static void die(int code) NOINST;
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}

static void report(const char *text) NOINST;
static void report(const char *text)
{
    u32 len = 0;
    while (text[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static int failures;

static void ok(const char *name) NOINST;
static void ok(const char *name)
{
    report("  ok   ");
    report(name);
    report("\n");
}

static void fail(const char *name) NOINST;
static void fail(const char *name)
{
    report("  FAIL ");
    report(name);
    report("\n");
    failures++;
}

static void check(int cond, const char *name) NOINST;
static void check(int cond, const char *name)
{
    if (cond) ok(name); else fail(name);
}

/* 4 本を「起動 → 最初の OP_WAIT で park」まで進める共通の下ごしらえ。 */
static void fill_four(MaState *st, u32 pages_each) NOINST;
static void fill_four(MaState *st, u32 pages_each)
{
    int i;
    for (i = 0; i < 4; i++) {
        ma_start(st, pages_each, 1);
        ma_gui_call(st, MA_OP_WAIT);
        ma_park(st);
    }
}

/* ---- 1. 5 本目は ERR_FULL、既存 4 本は無事 (票 G3) ---- */
static void case_fifth_refused(void) NOINST;
static void case_fifth_refused(void)
{
    MaState st;
    int ids[4], i, r;
    ma_init(&st, 4096);
    for (i = 0; i < 4; i++) {
        ids[i] = ma_start(&st, 100, 1);
        ma_gui_call(&st, MA_OP_WAIT);
        ma_park(&st);
    }
    check(ids[0] == 2 && ids[1] == 3 && ids[2] == 4 && ids[3] == 5,
          "1a ID は 2..5 が小さい順に配られる");
    r = ma_start(&st, 100, 1);
    check(r == MA_ERR_FULL, "1b 5 本目は ERR_FULL");
    check(ma_live(&st) == 4, "1c 5 本目の拒否で既存 4 本は減らない");
    check(st.free_pages == 4096 - 400,
          "1d 拒否された 5 本目はページを 1 枚も取らない");
    check(st.cur == MA_SHELL_ID && st.owner == MA_SHELL_ID,
          "1e 拒否のあと WM top-level のまま");
}

/* ---- 2. 終了は 1 本分だけ回収する (票 G2) ---- */
static void case_exit_reclaims_one(void) NOINST;
static void case_exit_reclaims_one(void)
{
    MaState st;
    MaApp *other;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    /* ID 2 と ID 3 に資源を積む */
    ma_resume(&st, 2);
    ma_res_add(&st, MA_RES_FD, 3);
    ma_res_add(&st, MA_RES_WINDOW, 2);
    ma_gui_call(&st, MA_OP_WAIT);
    ma_park(&st);
    ma_resume(&st, 3);
    ma_res_add(&st, MA_RES_FD, 5);
    ma_res_add(&st, MA_RES_TIMER, 1);
    /* ID 3 が終わる */
    ma_exit(&st, 0);

    check(ma_live(&st) == 3, "2a 終了で生きている本数が 1 本だけ減る");
    check(st.reclaims == 1 && st.last_reclaim_id == 3,
          "2b 回収は終了した ID に対して 1 回だけ");
    check(st.exited_id == 3, "2c gui_owner_exit は終了した ID で呼ばれる");
    check(st.free_pages == 4096 - 300, "2d ページは 1 本分だけ返る");
    other = ma_app(&st, 2);
    check(other != 0 && other->res[MA_RES_FD] == 3 &&
          other->res[MA_RES_WINDOW] == 2,
          "2e 他のアプリの資源は 1 つも触られない");
    check(ma_app(&st, 3) == 0, "2f 終了した ID は空く");
    check(st.cur == MA_SHELL_ID, "2g 終了後は WM top-level へ戻る");
}

/* ---- 3. 切替は OP_WAIT の中でだけ (票 G7 / 契約 T2a) ---- */
static void case_switch_only_in_op_wait(void) NOINST;
static void case_switch_only_in_op_wait(void)
{
    MaState st;
    u32 before;
    ma_init(&st, 4096);
    ma_start(&st, 100, 1);
    check(ma_park(&st) == MA_ERR_STATE,
          "3a gui_call の外では park できない");
    ma_gui_call(&st, MA_OP_POLL);
    check(ma_park(&st) == MA_ERR_STATE,
          "3b OP_POLL の中では park できない");
    ma_gui_return(&st);
    ma_gui_call(&st, MA_OP_COMMIT);
    check(ma_park(&st) == MA_ERR_STATE,
          "3c OP_COMMIT の中では park できない");
    ma_gui_return(&st);
    before = st.pd_switches;
    check(st.pd_switches == before, "3d park に失敗した間 CR3 は動かない");
    ma_gui_call(&st, MA_OP_WAIT);
    check(ma_park(&st) == MA_OK, "3e OP_WAIT の中でだけ park できる");
    check(st.owner == MA_SHELL_ID, "3f park で owner は 1 (WM) に戻る");
    check(ma_resume(&st, 2) == MA_OK, "3g top-level から resume できる");
    check(st.pd_switches == before + 1, "3h CR3 が動くのは resume の 1 回だけ");
}

/* ---- 4. 走っているアプリの横取りは起きない ---- */
static void case_no_preemption(void) NOINST;
static void case_no_preemption(void)
{
    MaState st;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    ma_resume(&st, 2);
    check(ma_resume(&st, 3) == MA_ERR_STATE,
          "4a アプリが走っている間は別のアプリを起こせない");
    check(st.cur == 2, "4b 横取りが弾かれても走っているのは同じ 1 本");
    check(ma_kill(&st, 3) == MA_ERR_STATE,
          "4c アプリが走っている間は WM の kill も走らない");
}

/* ---- 5. fault は他のアプリの資源に触らない (票 G5) ---- */
static void case_fault_isolated(void) NOINST;
static void case_fault_isolated(void)
{
    MaState st;
    MaApp *a2, *a4;
    int i;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    for (i = 2; i <= 5; i++) {
        ma_resume(&st, i);
        ma_res_add(&st, MA_RES_FD, i);
        ma_res_add(&st, MA_RES_SHM, 1);
        ma_gui_call(&st, MA_OP_WAIT);
        ma_park(&st);
    }
    ma_resume(&st, 3);
    check(ma_fault(&st) == MA_OK, "5a 走っているアプリは fault で畳める");
    check(ma_live(&st) == 3, "5b fault で減るのは 1 本だけ");
    a2 = ma_app(&st, 2);
    a4 = ma_app(&st, 4);
    check(a2 != 0 && a2->res[MA_RES_FD] == 2 && a2->res[MA_RES_SHM] == 1,
          "5c fault の前後で ID 2 の資源は不変");
    check(a4 != 0 && a4->res[MA_RES_FD] == 4 && a4->res[MA_RES_SHM] == 1,
          "5d fault の前後で ID 4 の資源は不変");
    check(st.free_pages == 4096 - 300, "5e 返るページは 1 本分だけ");
}

/* ---- 6. スロットと ID の対応は一意で、park/resume で動かない ---- */
static void case_slot_bijection(void) NOINST;
static void case_slot_bijection(void)
{
    MaState st;
    MaApp *a;
    int i, j, s[4], id;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    for (i = 0; i < 4; i++) {
        a = ma_app(&st, MA_ID_MIN + i);
        s[i] = a ? a->slot : -1;
    }
    check(s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3,
          "6a スロットは 0..3 が小さい順に配られる");
    for (i = 0; i < 4; i++) {
        for (j = i + 1; j < 4; j++) {
            if (s[i] == s[j]) { fail("6b スロットが重複しない"); return; }
        }
    }
    ok("6b スロットが重複しない");
    ma_resume(&st, 4);
    ma_gui_call(&st, MA_OP_WAIT);
    ma_park(&st);
    a = ma_app(&st, 4);
    check(a != 0 && a->slot == 2, "6c park/resume でスロットは動かない");
    /* ID 3 を畳むと、そのスロットだけが空いて次の起動へ回る */
    ma_resume(&st, 3);
    ma_exit(&st, 0);
    id = ma_start(&st, 100, 1);
    a = ma_app(&st, id);
    check(id == 3 && a != 0 && a->slot == 1,
          "6d 空いた ID とスロットが次の起動へ回る");
}

/* ---- 7. メモリが足りなければ拒否する (票 §K5a-5、スワップしない) ---- */
static void case_nomem_refuses(void) NOINST;
static void case_nomem_refuses(void)
{
    MaState st;
    int r;
    ma_init(&st, 250);
    ma_start(&st, 100, 1);
    ma_gui_call(&st, MA_OP_WAIT);
    ma_park(&st);
    ma_start(&st, 100, 1);
    ma_gui_call(&st, MA_OP_WAIT);
    ma_park(&st);
    r = ma_start(&st, 100, 1);
    check(r == MA_ERR_NOMEM, "7a 入らない要求は ERR_NOMEM で拒否");
    check(ma_live(&st) == 2, "7b 拒否で既存のアプリは減らない");
    check(st.free_pages == 50, "7c 拒否は空きページを 1 枚も動かさない");
    check(ma_start(&st, 50, 1) == 4, "7d 入る大きさなら 3 本目も起動できる");
}

/* ---- 8. CUI 互換: 入れ子 exec_run では 段 = ID ---- */
static void case_cui_nesting(void) NOINST;
static void case_cui_nesting(void)
{
    MaState st;
    int a, b, c;
    ma_init(&st, 4096);
    /* シェル (ID 1) から 3 段まで入れ子で起動する。GUI ではないのでスロット無し。 */
    a = ma_start(&st, 10, 0);
    b = ma_start(&st, 10, 0);
    c = ma_start(&st, 10, 0);
    check(a == 2 && b == 3 && c == 4,
          "8a 入れ子 exec の ID は段の深さと同じ (2,3,4)");
    {
        MaApp *c2 = ma_app(&st, 2);
        MaApp *c4 = ma_app(&st, 4);
        check(c2 != 0 && c4 != 0 && c2->slot == -1 && c4->slot == -1,
              "8b CUI の子は GUI スロットを取らない");
    }
    check(st.owner == 4, "8c 走っているのは最も深い段");
    ma_exit(&st, 0);
    check(st.cur == 3 && st.owner == 3, "8d 終了で 1 段だけ親へ戻る");
    ma_exit(&st, 0);
    check(st.cur == 2 && st.owner == 2, "8e さらに 1 段戻る");
    ma_exit(&st, 0);
    check(st.cur == MA_SHELL_ID && st.owner == MA_SHELL_ID,
          "8f 最後はシェル (ID 1) へ戻る");
    check(ma_live(&st) == 0 && st.free_pages == 4096,
          "8g 3 段ぶんのページが全部返る");
    /* 池は 1 つ: GUI 4 本が居れば入れ子の 5 本目は立たない */
    fill_four(&st, 10);
    check(ma_start(&st, 10, 0) == MA_ERR_FULL,
          "8h GUI 4 本のときは CUI の入れ子も ERR_FULL");
}

/* ---- 9. CTRL+STOP は走っているアプリ宛にしか立たない (票 G4) ---- */
static void case_abort_targets_running(void) NOINST;
static void case_abort_targets_running(void)
{
    MaState st;
    MaApp *a;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    ma_resume(&st, 3);
    ma_abort_request(&st);
    a = ma_app(&st, 2);
    check(a != 0 && a->abort_req == 0,
          "9a 止めてあるアプリに CTRL+STOP は立たない");
    a = ma_app(&st, 3);
    check(a != 0 && a->abort_req == 1, "9b 走っているアプリにだけ立つ");
    check(ma_abort_check(&st) == MA_OK, "9c 次の安全地点で畳まれる");
    check(ma_live(&st) == 3 && ma_app(&st, 3) == 0,
          "9d 畳まれるのはその 1 本だけ");
    check(st.cur == MA_SHELL_ID, "9e 畳んだあと WM top-level へ戻る");
    /* 止めてあるアプリは WM からの kill でしか畳めない */
    check(ma_kill(&st, 4) == MA_OK, "9f 止めてあるアプリは kill で畳める");
    check(ma_live(&st) == 2, "9g kill も 1 本だけ");
}

/* ---- 10. 起動失敗はどの状態も動かさない ---- */
static void case_failed_start_is_clean(void) NOINST;
static void case_failed_start_is_clean(void)
{
    MaState st;
    u32 sw;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    sw = st.pd_switches;
    check(ma_start(&st, 100, 1) == MA_ERR_FULL, "10a 5 本目は起動しない");
    check(st.pd_switches == sw, "10b 起動失敗では CR3 を載せ替えない");
    check(st.owner == MA_SHELL_ID, "10c 起動失敗で owner は 1 のまま");
    check(st.reclaims == 0, "10d 起動失敗では回収を回さない");
}

/* ---- 11. GUI の起動は WM top-level からだけ (契約 V12-S の S2) ---- */
static void case_gui_start_only_from_toplevel(void) NOINST;
static void case_gui_start_only_from_toplevel(void)
{
    MaState st;
    ma_init(&st, 4096);
    ma_start(&st, 100, 1);          /* ID 2 が走り出す */
    check(ma_start(&st, 100, 1) == MA_ERR_STATE,
          "11a 走っているアプリの中から GUI アプリは起動できない");
    check(ma_live(&st) == 1, "11b 弾かれた起動は本数を増やさない");
    check(st.free_pages == 4096 - 100, "11c 弾かれた起動はページを取らない");
    check(st.cur == 2, "11d 弾かれても走っているのは元のアプリのまま");
    /* CUI の入れ子 (gui=0) は従来どおり走っているアプリからでも起動できる */
    check(ma_start(&st, 10, 0) == 3,
          "11e CUI の入れ子 exec_run は従来どおり通る");
}

/* ---- 12. 同時 ready の選択規則 (D11) ---- */
static void case_pick_rule(void) NOINST;
static void case_pick_rule(void)
{
    MaState st;
    ma_init(&st, 4096);
    fill_four(&st, 100);

    /* 12a フォーカス窓の owner が入力群にいれば、それを最優先 */
    ma_set_ready(&st, 3, 1, 0);
    ma_set_ready(&st, 5, 1, 0);
    st.focus = 5;
    st.last_run = 2;
    check(ma_pick(&st) == 5, "12a 入力群にフォーカスが居ればフォーカスを選ぶ");

    /* 12b フォーカスが入力群に居なければ last_run+1 から ID 昇順に巡回 */
    st.focus = 2;              /* ID 2 は ready でない */
    st.last_run = 3;           /* 巡回は 4 → 5 → 2 → 3 */
    check(ma_pick(&st) == 5, "12b 入力群はラウンドロビン (last_run の次から)");

    /* 12c 入力群が空なら導出群を同じ巡回で */
    ma_set_ready(&st, 3, 0, 0);
    ma_set_ready(&st, 5, 0, 0);
    ma_set_ready(&st, 2, 0, 1);
    ma_set_ready(&st, 4, 0, 1);
    st.focus = 2;
    st.last_run = 2;           /* 巡回は 3 → 4 → 5 → 2 */
    check(ma_pick(&st) == 4, "12c 導出群もラウンドロビン");
    /* 12d 導出群ではフォーカスを優先しない: フォーカス (4) が導出群に居ても、
     *     巡回の順が先の 2 が選ばれる (優先していれば 4 になる)。 */
    st.focus = 4;
    st.last_run = 4;           /* 巡回は 5 → 2 → 3 → 4 */
    check(ma_pick(&st) == 2, "12d 導出群ではフォーカスを優先しない");

    /* 12e 入力は導出より必ず先 */
    ma_set_ready(&st, 5, 1, 0);
    st.focus = 2;
    st.last_run = 4;
    check(ma_pick(&st) == 5, "12e 入力群は導出群より先");

    /* 12f 誰も ready でなければ起こさない */
    ma_set_ready(&st, 2, 0, 0);
    ma_set_ready(&st, 4, 0, 0);
    ma_set_ready(&st, 5, 0, 0);
    check(ma_pick(&st) == 0, "12f ready が 1 本も無ければ誰も起こさない");
}

/* ---- 13. 走っているアプリが park すべきか (D11 (1)) ---- */
static void case_park_decision(void) NOINST;
static void case_park_decision(void)
{
    MaState st;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    ma_resume(&st, 2);
    ma_gui_call(&st, MA_OP_WAIT);

    ma_set_ready(&st, 2, 1, 0);
    ma_set_ready(&st, 4, 1, 0);
    check(ma_should_park(&st) == 0, "13a 自分に入力があれば park しない");

    ma_set_ready(&st, 2, 0, 1);
    ma_set_ready(&st, 4, 0, 0);
    check(ma_should_park(&st) == 0,
          "13b 他に ready が居なければ park しない (1 本のときの回帰ゼロ)");

    ma_set_ready(&st, 4, 1, 0);
    check(ma_should_park(&st) == 1, "13c 他に入力があれば導出だけの自分は譲る");

    ma_set_ready(&st, 4, 0, 1);
    check(ma_should_park(&st) == 1, "13d 他も導出だけなら巡回のため譲る");

    ma_set_ready(&st, 2, 0, 0);
    check(ma_should_park(&st) == 1, "13e 自分が ready でなければ譲る");
}

/* ---- 14. 飢餓が起きない (導出群だけの 4 本が 1 周で全員走る) ---- */
static void case_no_starvation(void) NOINST;
static void case_no_starvation(void)
{
    MaState st;
    int seen[MA_MAX_APPS];
    int order[4];
    int i, k;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    for (i = 0; i < MA_MAX_APPS; i++) seen[i] = 0;
    for (i = MA_ID_MIN; i <= MA_ID_MAX; i++) ma_set_ready(&st, i, 0, 1);
    st.focus = 2;               /* フォーカスは固定。飢餓の原因にならないこと */
    st.last_run = 0;            /* まだ誰も走っていない */

    for (k = 0; k < 4; k++) {
        int id = ma_pick(&st);
        order[k] = id;
        if (id < MA_ID_MIN || id > MA_ID_MAX) { fail("14a 4 周とも 1 本選べる"); return; }
        seen[id - MA_ID_MIN]++;
        ma_resume(&st, id);     /* last_run が進む */
        ma_gui_call(&st, MA_OP_WAIT);
        ma_park(&st);
    }
    ok("14a 4 周とも 1 本選べる");
    check(order[0] == 2 && order[1] == 3 && order[2] == 4 && order[3] == 5,
          "14b 巡回の順は ID 昇順 (2,3,4,5)");
    check(seen[0] == 1 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1,
          "14c 4 周で全員がちょうど 1 回ずつ走る (飢餓なし)");
    check(ma_pick(&st) == 2, "14d 1 周したら先頭へ戻る");
}

/* ---- 15. 自作入力で park を回避できないこと ----
 *  独立レビュー 2026-09-10 [P2] の反例。アプリ A が自分の窓 2 枚へ交互に
 *  set_focus() すると、`emit_focus_change` が **両窓の owner** へ Focus を流す
 *  (userland/gshell/src/input.rs:855-872) ため、A 自身に待ち行列型が湧き続ける。
 *  「入力群は人間由来だから有限」は成立しない。 */
#define MA_STARVE_LIMIT 200
static void case_self_input_cannot_starve(void) NOINST;
static void case_self_input_cannot_starve(void)
{
    MaState st;
    int i, b_ran = 0, b_at = 0, parks = 0;
    ma_init(&st, 4096);
    /* A = ID 2 (毎周 自分に Focus を湧かせる)、B = ID 3 (Paint 待ちで park 中)。 */
    ma_start(&st, 100, 1); ma_gui_call(&st, MA_OP_WAIT); ma_park(&st);
    ma_start(&st, 100, 1); ma_gui_call(&st, MA_OP_WAIT); ma_park(&st);
    ma_set_ready(&st, 3, 0, 1);          /* B は導出型 (Paint) で ready */
    st.focus = 2;                        /* A がフォーカスを握ったまま */
    ma_resume(&st, 2);

    for (i = 0; i < MA_STARVE_LIMIT; i++) {
        ma_gui_call(&st, MA_OP_WAIT);
        ma_set_ready(&st, 2, 1, 0);      /* A が自分で湧かせた Focus */
        if (ma_should_park(&st)) {
            int k;
            ma_park(&st);
            parks++;
            k = ma_pick(&st);
            if (k <= 0) { fail("15a 自作入力を続けても park は必ず起きる"); return; }
            ma_resume(&st, k);
        } else {
            ma_gui_return(&st);
        }
        if (st.cur == 3 && !b_ran) { b_ran = 1; b_at = i + 1; }
    }
    check(parks > 0, "15a 自作入力を続けても park は必ず起きる");
    check(b_ran, "15b Paint 待ちの B が走る (飢餓しない)");
    /* 15c 上限の式は生きているアプリ数 N に依る。ここは N=2 なので
     *     (2N-2) x (STREAK_MAX+1) = 2 x 5 = 10 にちょうど届く。
     *     定数 MA_STARVE_BOUND は最大値 (N=4 の 30) なので、その内側でもある。 */
    check(b_ran && b_at == (2 * 2 - 2) * (MA_INPUT_STREAK_MAX + 1),
          "15c B は N=2 の上限 (10 回) にちょうど届く");
    check(b_ran && b_at <= MA_STARVE_BOUND,
          "15d B は定数の上限 (MA_STARVE_BOUND) の内側");
}

/* ---- 16. ラウンドをまたぐ待ちの上限 ----
 *  独立レビュー 2026-09-10 [P2] (2 回目) の反例。無限待ちは回 12 で消えたが、
 *  「1 ラウンドは最大 MA_MAX_APPS turn」から「任意の時点から 1 ラウンドぶんで
 *  再開できる」は導けない。自分が park した時点では
 *    (i)  現ラウンドの残り (自分以外の未使用 turn) ≤ N-1 turn
 *    (ii) 次ラウンドで自分より先に選ばれる分            ≤ N-1 turn
 *  の**両方**が待ち時間になる。導出群の A は、入力群を維持する B/C/D に
 *  2 ラウンド続けて先を越される。 */
static int run_cross_round(int focus_id, int launch_mode) NOINST;
static int run_cross_round(int focus_id, int launch_mode)
{
    MaState st;
    int i, n, k;
    ma_init(&st, 4096);
    for (i = 0; i < 4; i++) {
        ma_start(&st, 100, 1);
        ma_gui_call(&st, MA_OP_WAIT);
        ma_park(&st);
    }
    /* 4 本とも入力 ready。フォーカスを A に置いてラウンド先頭で A を走らせる。 */
    for (i = MA_ID_MIN; i <= MA_ID_MAX; i++) ma_set_ready(&st, i, 1, 0);
    st.focus = MA_ID_MIN;
    k = ma_pick(&st);
    if (k != MA_ID_MIN) return -1;
    ma_resume(&st, k);
    /* A は入力を消費し、Paint だけ残す = 以後ずっと導出群で ready。
     * B/C/D は入力 ready を維持し続ける (消費しない)。 */
    ma_set_ready(&st, MA_ID_MIN, 0, 1);
    st.focus = focus_id;
    /* launch_mode: 0 = 保留なし / 1 = 最初の park で top-level が消費 /
     *              2 = ずっと保留のまま (上界が崩れないことを見る)。 */
    st.launch_pending = (launch_mode != 0);
    /* n = 0 が「A が park する OP_WAIT」= 起算点。 */
    for (n = 0; n < 200; n++) {
        ma_gui_call(&st, MA_OP_WAIT);
        if (ma_should_park(&st)) {
            ma_park(&st);
            if (launch_mode == 1) st.launch_pending = 0;   /* top-level が消費した */
            k = ma_pick(&st);
            if (k <= 0) return -2;
            ma_resume(&st, k);
            if (k == MA_ID_MIN) return n;   /* A が走った */
        } else {
            ma_gui_return(&st);
        }
    }
    return 0;                               /* 200 回まわしても走らなかった */
}

static void case_cross_round_bound(void) NOINST;
static void case_cross_round_bound(void)
{
    int a2, a3, a5;
    a2 = run_cross_round(MA_ID_MIN, 0);        /* フォーカス = A */
    a3 = run_cross_round(MA_ID_MIN + 1, 0);    /* フォーカス = B */
    a5 = run_cross_round(MA_ID_MAX, 0);        /* フォーカス = D */
    check(a2 > 0, "16a 導出群の A は必ず走る (無限待ちにならない)");
    check(a2 <= MA_STARVE_BOUND, "16b A は再導出した上限以内に走る");
    check(a2 == MA_STARVE_BOUND,
          "16c 上限は緩くない (この構成でちょうど上限に届く)");
    check(a3 > 0 && a3 <= MA_STARVE_BOUND,
          "16d フォーカスが B でも上限を超えない");
    check(a5 > 0 && a5 <= MA_STARVE_BOUND,
          "16e フォーカスが D でも上限を超えない");
}

/* ---- 17. top-level にしか出来ない仕事 (LAUNCH の保留) があれば譲る ----
 *  PM 受入 2026-09-11 (K5b-W からの提案、票 D11-3)。`exec_start` は WM の
 *  top-level からしか呼べず (契約 S2 = ケース 11)、走っているアプリが park
 *  しない限り top-level へ戻る道は無い。したがってこの分岐が無いと、アプリが
 *  1 本走っている間は 2 本目を**永久に**起動できない (ケース 13b の「他に
 *  ready が居なければ park しない」に当たって譲らないため)。
 *  起動要求は有限個の事象で、消費されれば条件も消えるので、D11-3a の上界
 *  (30 = (2N−2) × (STREAK_MAX+1)) は変わらない — 17g / 17h で見る。 */
static void case_launch_pending_parks(void) NOINST;
static void case_launch_pending_parks(void)
{
    MaState st;
    int streak_before, n_none, n_consumed, n_held;
    ma_init(&st, 4096);
    fill_four(&st, 100);
    ma_resume(&st, 2);
    ma_gui_call(&st, MA_OP_WAIT);

    /* 自分だけが ready = 従来なら「他に ready が居ない」で譲らない場面。 */
    ma_set_ready(&st, 2, 1, 0);
    check(ma_should_park(&st) == 0,
          "17a 保留が無ければ従来どおり park しない (他に ready が居ない)");
    st.launch_pending = 1;
    check(ma_should_park(&st) == 1,
          "17b LAUNCH が保留なら、他に ready が居なくても譲る");
    st.launch_pending = 0;
    check(ma_should_park(&st) == 0, "17c 保留が消えれば従来どおりに戻る");

    /* 入力の据え置き (input_streak) より保留が先であること。 */
    ma_set_ready(&st, 4, 1, 0);          /* 他にも入力群が居る */
    st.input_streak = 0;
    check(ma_should_park(&st) == 0 && st.input_streak == 1,
          "17d 保留が無ければ自分の入力を据え置く (従来 = 13a)");
    st.launch_pending = 1;
    streak_before = st.input_streak;
    check(ma_should_park(&st) == 1, "17e 保留は入力の据え置きより先に効く");
    check(st.input_streak == streak_before,
          "17f 保留での park は据え置きの数え (input_streak) を動かさない");

    /* D11-3a の上界。ケース 16 と同じ構成で保留の有無だけを変える。 */
    n_none     = run_cross_round(MA_ID_MIN, 0);
    n_consumed = run_cross_round(MA_ID_MIN, 1);
    n_held     = run_cross_round(MA_ID_MIN, 2);
    check(n_consumed == n_none && n_none == MA_STARVE_BOUND,
          "17g 保留が 1 回で消費されれば待ち回数は従来と同じ (= 30)");
    check(n_held > 0 && n_held <= MA_STARVE_BOUND,
          "17h 保留が続いても D11-3a の上界 (30) を超えない");
}


/* ---- 18. 鍵待ち (WAIT_KEY) が譲り合いに加わる (票 K7 D3 / 指摘 A・C) ----
 *  端末アプリが `kbd_inject` で積んだバイトが起床の理由になる。鍵待ちで
 *  止まるのはスロットを持たない CUI の入れ子の子なので input_ready /
 *  derived_ready はどちらも立たない — **注入リングの未読が唯一の ready
 *  条件**になる (指摘 C)。D11 の規則 (ラウンドの turn・ID 昇順の巡回・
 *  入力優先の上限) と上界 (30) は 1 つも変えない。 */
static void case_wait_key_joins_the_round(void) NOINST;
static void case_wait_key_joins_the_round(void)
{
    MaState st;
    int id, i;

    ma_init(&st, 4096);
    ma_start(&st, 100, 1);                 /* ID 2 = GUI アプリ (スロット 0) */
    ma_gui_call(&st, MA_OP_WAIT);
    ma_park(&st);
    id = ma_start(&st, 100, 0);            /* ID 3 = CUI (スロット無し) */
    check(id == MA_ID_MIN + 1 && st.app[1].slot == -1,
          "18a スロットを持たない CUI の入れ子の子が立つ");

    /* (1) 第 2 の park 点。OP_WAIT の中に居なくても止まる (D1)。 */
    check(st.app[1].in_op_wait == 0, "18b OP_WAIT の中には居ない");
    check(ma_park_kbd(&st) == MA_OK, "18c kbd 待ちは第 2 の park 点");
    check(st.app[1].state == MA_WAIT_KEY && st.cur == MA_SHELL_ID,
          "18d park すると WAIT_KEY になり top-level へ戻る");

    /* (2) 注入リングが空なら起こさない。resume も AGAIN で拒む (指摘 B/C)。 */
    st.kbd_pending = 0;
    ma_set_ready(&st, 2, 0, 0);
    check(ma_pick(&st) == 0, "18e pending 0 の WAIT_KEY は選ばれない");
    check(ma_resume(&st, MA_ID_MIN + 1) == MA_ERR_AGAIN,
          "18f 空の注入で起こそうとすると AGAIN");
    check(st.app[1].state == MA_WAIT_KEY, "18g AGAIN でも印は残る");
    check(st.app[1].turn_used == 0, "18h AGAIN は turn を使わない");

    /* (3) 1 バイト積めば入力群として選ばれ、resume が 1 バイト取り出す。 */
    st.kbd_pending = 1;
    check(ma_pick(&st) == MA_ID_MIN + 1, "18i pending > 0 で WAIT_KEY を選ぶ");
    check(ma_resume(&st, MA_ID_MIN + 1) == MA_OK, "18j 鍵待ちを起こせる");
    check(st.kbd_pending == 0, "18k resume が 1 バイト取り出す");
    check(st.app[1].state == MA_RUNNING && st.cur == MA_ID_MIN + 1,
          "18l resume で走り出す");

    /* (4) 入力群の中の順は D11 のまま (last_run の次から ID 昇順)。 */
    check(ma_park_kbd(&st) == MA_OK, "18m また鍵待ちに入る");
    st.kbd_pending = 1;
    ma_set_ready(&st, 2, 1, 0);            /* ID 2 も入力群 */
    st.last_run = MA_ID_MIN + 1;           /* 直前は 3 → 巡回は 2 から */
    st.focus = 0;
    for (i = 0; i < MA_MAX_APPS; i++) st.app[i].turn_used = 0;
    check(ma_pick(&st) == MA_ID_MIN, "18n 巡回の順は WAIT_KEY でも変わらない");

    /* (5) 走っているアプリは「鍵待ちが起きられる」を ready として数える。 */
    ma_resume(&st, MA_ID_MIN);
    ma_gui_call(&st, MA_OP_WAIT);
    ma_set_ready(&st, 2, 0, 0);
    check(ma_should_park(&st) == 1,
          "18o 鍵待ちが起きられるなら走っている本人は譲る");
    st.kbd_pending = 0;
    check(ma_should_park(&st) == 0,
          "18p 注入が空なら従来どおり譲らない (他に ready が居ない)");

    /* (6) 鍵待ちのまま畳める (D5 / 指摘 D)。 */
    ma_park(&st);
    check(ma_kill(&st, MA_ID_MIN + 1) == MA_OK, "18q 鍵待ちを kill で畳める");
    check(ma_live(&st) == 1, "18r 畳んだのは 1 本だけ");
}

/* ------------------------------------------------------------------ */
/*  ケース 19 (票 T8 §7 D8) — ポーリング型の協調 yield は「最下位」     */
/*                                                                     */
/*  GUI 中に kbd_trygetchar をポーリングする全画面 GFX プログラムは     */
/*  第 3 の park 点 (MA_WAIT_POLL) で 1 周だけ WM へ譲る。**常に        */
/*  ready** な poll 群は最下位 — 入力群 / 導出群 / LAUNCH 保留の        */
/*  どれも無い周にしか選ばない (複数居れば ID 昇順)。D11 の規則と       */
/*  上界 (30) は不変。入力/導出が空の WAIT_POLL は round に数えない。   */
/* ------------------------------------------------------------------ */
static void case_poll_yield_is_lowest_priority(void) NOINST;
static void case_poll_yield_is_lowest_priority(void)
{
    MaState st;
    int id;

    ma_init(&st, 4096);
    ma_start(&st, 100, 1);                 /* ID 2 = GUI アプリ (スロット 0) */
    ma_gui_call(&st, MA_OP_WAIT);
    ma_park(&st);
    id = ma_start(&st, 100, 0);            /* ID 3 = 端末から起動した全画面 GFX */
    check(id == MA_ID_MIN + 1 && st.app[1].slot == -1,
          "19a スロットを持たない全画面 GFX が立つ");

    /* (1) 第 3 の park 点。OP_WAIT の中に居なくても止まる。 */
    check(ma_park_poll(&st) == MA_OK, "19b ポーリングは第 3 の park 点");
    check(st.app[1].state == MA_WAIT_POLL && st.cur == MA_SHELL_ID,
          "19c park すると WAIT_POLL になり top-level へ戻る");

    /* (2) 他が ready な周は選ばれない (最下位)。 */
    ma_set_ready(&st, 2, 1, 0);
    check(ma_pick(&st) == MA_ID_MIN, "19d 入力群が居る周は WAIT_POLL を選ばない");
    st.app[0].turn_used = 0;
    ma_set_ready(&st, 2, 0, 1);
    check(ma_pick(&st) == MA_ID_MIN, "19e 導出群が居る周も WAIT_POLL を選ばない");

    /* (3) 誰も ready でない周にだけ起こす。 */
    st.app[0].turn_used = 0;
    ma_set_ready(&st, 2, 0, 0);
    check(ma_pick(&st) == MA_ID_MIN + 1,
          "19f 他に ready が無ければ WAIT_POLL を選ぶ");

    /* (4) top-level にしか出来ない仕事 (LAUNCH の保留) がある周も譲らない。 */
    st.launch_pending = 1;
    check(ma_pick(&st) == 0, "19g LAUNCH 保留の周は WAIT_POLL を選ばない");
    st.launch_pending = 0;

    /* (5) 起こせる。注入リングが空でも AGAIN にならない (D8: EAX = -1)。 */
    st.kbd_pending = 0;
    check(ma_resume(&st, MA_ID_MIN + 1) == MA_OK,
          "19h 空の注入でも WAIT_POLL は起こせる");
    check(st.app[1].state == MA_RUNNING && st.cur == MA_ID_MIN + 1,
          "19i resume で走り出す");

    /* (6) 走っている本人は「ポーリングが居る」だけで譲る (実機 F8 の修正)。
     *     譲らないと op_wait の中を回るだけで ma_pick_poll へ行けない。
     *     ID 2 を直接ポーリング中にする。 */
    st.app[0].state = MA_WAIT_POLL;
    ma_gui_call(&st, MA_OP_WAIT);
    check(ma_should_park(&st) == 1,
          "19j WAIT_POLL が居るなら走っている本人は譲る");
    /* 据え置き (input_streak) は ready のときと同じに効く。 */
    st.input_streak = 0;
    st.app[1].input_ready = 1;
    check(ma_should_park(&st) == 0, "19k 自分の入力は据え置ける");
    st.input_streak = MA_INPUT_STREAK_MAX;
    check(ma_should_park(&st) == 1, "19l 据え置きの上限を超えたら譲る");
    st.app[1].input_ready = 0;
    /* ポーリングが 1 本も居なければ従来どおり譲らない (回帰ゼロ)。 */
    st.app[0].state = MA_PARKED;
    ma_set_ready(&st, 2, 0, 0);
    check(ma_should_park(&st) == 0, "19m 誰も居なければ従来どおり譲らない");
    ma_set_ready(&st, 2, 1, 0);
    check(ma_should_park(&st) == 1, "19n 入力群が居れば従来どおり譲る");

    /* (7) 複数のポーリングは ID 昇順 (last_run の巡回には乗せない)。 */
    ma_set_ready(&st, 2, 0, 0);
    st.app[0].state = MA_WAIT_POLL;
    check(ma_park_poll(&st) == MA_OK, "19o 走っている本人もまた譲る");
    st.last_run = MA_ID_MIN;               /* 巡回なら 3 から。ID 昇順なら 2 */
    st.focus = 0;
    st.app[0].turn_used = 0;
    st.app[1].turn_used = 0;
    check(ma_pick(&st) == MA_ID_MIN, "19p ポーリングが複数なら ID 昇順");
}

/* g3fix: same state/input matrix as the real Rust WM probe. */
static void case_poll_gui_ready(void) NOINST;
static void case_poll_gui_ready(void)
{
    MaState st;
    for (int state = MA_PARKED; state <= MA_WAIT_POLL; state++) {
        if (state == MA_WAIT_KEY) continue;
        for (int input = 0; input <= 1; input++) {
            for (int derived = 0; derived <= 1; derived++) {
                ma_init(&st, 4096);
                ma_start(&st, 100, 1);
                ma_gui_call(&st, MA_OP_WAIT); ma_park(&st);
                ma_start(&st, 100, 1);
                ma_gui_call(&st, MA_OP_WAIT); ma_park(&st);
                st.app[0].state = state;
                st.focus = 2;
                ma_set_ready(&st, 2, input, derived);
                ma_set_ready(&st, 3, 1, 0);
                check(ma_input_group(&st, &st.app[0]) == input,
                      "20a GUI WAIT_POLL input matches product");
                check(ma_ready(&st, &st.app[0]) == (input || derived),
                      "20b GUI WAIT_POLL derived matches product");
                check(ma_pick(&st) == (input ? 2 : 3),
                      "20c foreground input wins, back input beats derived");
                ma_set_ready(&st, 3, 0, 0);
                check(ma_round_remaining(&st) == (input || derived),
                      "20d input/derived joins round, empty poll does not");
            }
        }
    }
}

int main(void) NOINST;
int main(void)
{
    failures = 0;
    report("multiapp model (K5a)\n");
    case_fifth_refused();
    case_exit_reclaims_one();
    case_switch_only_in_op_wait();
    case_no_preemption();
    case_fault_isolated();
    case_slot_bijection();
    case_nomem_refuses();
    case_cui_nesting();
    case_abort_targets_running();
    case_failed_start_is_clean();
    case_gui_start_only_from_toplevel();
    case_pick_rule();
    case_park_decision();
    case_no_starvation();
    case_self_input_cannot_starve();
    case_cross_round_bound();
    case_launch_pending_parks();
    case_wait_key_joins_the_round();
    case_poll_yield_is_lowest_priority();
    case_poll_gui_ready();
    if (failures) {
        report("FAILURES\n");
        die(1);
    }
    report("ALL PASS\n");
    die(0);
    return 0;
}

/* -nostdlib の入口 */
void _start(void) NOINST;
void _start(void)
{
    main();
}
