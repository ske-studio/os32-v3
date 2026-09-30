/* ======================================================================== */
/*  LAUNCH.C — 起動要求表 (票 docs/archive/gui_v13/TASK_T9_sh.md D3 / §1a)     */
/*                                                                          */
/*  設計の全文は include/launch.h の冒頭。ここに置かないもの:                */
/*    - exec_start / exec_kill そのもの → exec/exec.c (この表は「誰が何を    */
/*      頼んだか」しか持たない。カーネルは回収文脈から kill しない)          */
/*    - 「次にどれを取るか」以上の順番の規則 → WM (gshell)                    */
/*    - CR3 / setjmp / ページテーブル → exec/exec.c                          */
/*  だからこの .c はホストでそのまま試験できる                               */
/*  (tools/tests/launch_host.c が exec/appslot.c と組んで回す)。             */
/*                                                                          */
/*  不変条件 (壊れると実機では「起動しない」「プロンプトに戻らない」          */
/*  「ERR_FULL で固着」としか見えない):                                      */
/*    I1 表は要求者 ID ごとに 1 本。添字 = 要求者 ID (2〜5)。                */
/*    I2 照合は token。ID の再利用では取り違えない。                         */
/*    I3 child は配送状態 (phase / kind) と独立で、取消・退場でも消さない。  */
/*    I4 phase != IDLE の表は「その ID の枠が塞がっている」の意。孤児回収中  */
/*       も塞がったまま (再利用 ID からの launch_req は OS32_ERR_FULL)。     */
/*    I5 完了 (DONE / FAILED) の解放は poll が行う。ただし孤児の表は誰も     */
/*       poll しないので、完了と同時に IDLE へ落とす (票 §10 non-blocker 1)。*/
/* ======================================================================== */

#include "launch.h"
#include "appslot.h"
#include "con_sink.h"          /* con_sink_is_enabled: GUI 中かどうか */
#include "kstring.h"
#include "os32_kapi_shared.h"  /* OS32_ERR_* / LAUNCH_* / OS32X_FLAG_LAUNCHER */

/* res_owner_get は fs/fd_redirect.c。exec/ は -Ifs を持たないので、
 * exec/appslot.c と同じ流儀で extern 宣言する。 */
extern int res_owner_get(void);

/* ------------------------------------------------------------------------ */
/*  表                                                                       */
/* ------------------------------------------------------------------------ */

typedef struct {
    int  requester;                    /* 2〜5 / LAUNCH_REQ_ORPHAN */
    int  child;                        /* 所有する子 ID (0 = 無し) */
    int  phase;                        /* LAUNCH_PHASE_* */
    int  kind;                         /* LAUNCH_KIND_* */
    i32  token;                        /* 0 = 空き */
    i32  rc;                           /* FAILED のときの負値 */
    int  arg;                          /* KILL のとき畳む ID */
    char cmdline[LAUNCH_CMDLINE_MAX];  /* NUL 終端 */
} LaunchReq;

/* 添字 = 要求者 ID。0 と 1 (シェル帯) の枠は使わないが、添字をずらすと
 * 「表 = ID」が読めなくなるので APP_SLOT_COUNT 本そのまま持つ。 */
static LaunchReq g_req[APP_SLOT_COUNT];

/* token の種。**全体で単調増加**の 32bit (要求者 ID は混ぜない — 混ぜると
 * 上位が周回したとき別の要求者の古い token と衝突する。票 §9 blocker 4)。 */
static i32 g_next_token = 1;

/* 診断用 (KAPI にはしない。kernel.map の番地を emu_read_mem で読む)。 */
volatile u32 launch_req_count = 0;      /* 受け付けた要求の本数 */
volatile u32 launch_orphan_count = 0;   /* 孤児回収に回した表の本数 */

/* ------------------------------------------------------------------------ */
/*  素操作                                                                   */
/* ------------------------------------------------------------------------ */

static int id_ok(int id)
{
    return (id >= APP_ID_MIN && id <= APP_ID_MAX);
}

static void row_clear(LaunchReq *r)
{
    r->requester = 0;
    r->child = 0;
    r->phase = LAUNCH_PHASE_IDLE;
    r->kind = LAUNCH_KIND_NONE;
    r->token = 0;
    r->rc = 0;
    r->arg = 0;
    r->cmdline[0] = '\0';
}

/* token から表を引く (0 と負は「無効な token」なので必ず外す)。 */
static LaunchReq *row_by_token(i32 token)
{
    int i;
    if (token <= 0) return 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        if (g_req[i].phase != LAUNCH_PHASE_IDLE && g_req[i].token == token) {
            return &g_req[i];
        }
    }
    return 0;
}

/* 要求を終わらせる。孤児の表は誰も poll しないので、その場で解放する
 * (I5 / 票 §10 non-blocker 1)。そうしないと同じ ID の次の住人が永久に
 * OS32_ERR_FULL を食う。 */
static void row_finish(LaunchReq *r, int phase, i32 rc)
{
    if (r->requester == LAUNCH_REQ_ORPHAN) {
        row_clear(r);
        return;
    }
    r->phase = phase;
    r->rc = rc;
    r->kind = LAUNCH_KIND_NONE;
    r->arg = 0;
}

void launch_init(void)
{
    int i;
    for (i = 0; i < APP_SLOT_COUNT; i++) row_clear(&g_req[i]);
    /* token の種は戻さない — exec_init が再度呼ばれても、生きている
     * 要求者の手元にある古い token と衝突させないため。 */
}

/* ------------------------------------------------------------------------ */
/*  launch_req — 起動要求を積む (宣言 LAUNCHER を持つ CPL=3 だけ)            */
/* ------------------------------------------------------------------------ */
i32 launch_req(const char *cmdline)
{
    AppSlot *a;
    LaunchReq *r;
    int id;
    u32 len;

    if (cmdline == 0) return OS32_ERR_INVAL;

    /* GUI 中だけ。CUI では入れ子 exec_run が普通に使えるので表は要らない。 */
    if (!con_sink_is_enabled()) return OS32_ERR_INVAL;

    id = res_owner_get();
    if (!id_ok(id)) return OS32_ERR_INVAL;      /* WM top-level / シェル帯 */
    a = appslot_get(id);
    if (!a || !a->cpl3) return OS32_ERR_INVAL;
    /* 入れ子 exec_run の子 (gui == 0 の非シェル) は親が塞がっているので、
     * 起動要求を出しても poll で待つ相手が動かない (票 §0)。 */
    if (!a->gui) return OS32_ERR_INVAL;
    /* 宣言 (協調的な宣言であって認証ではない — 票 §8)。 */
    if ((a->hdr_flags & (u32)OS32X_FLAG_LAUNCHER) == 0) return OS32_ERR_INVAL;

    /* NUL 終端 1〜(LAUNCH_CMDLINE_MAX - 1) バイト。kstrlen は使えない —
     * CPL=3 のバッファが終端されていなければ走り抜けるため、上限で切る。 */
    len = 0;
    while (len < (u32)LAUNCH_CMDLINE_MAX && cmdline[len] != '\0') len++;
    if (len == 0 || len >= (u32)LAUNCH_CMDLINE_MAX) return OS32_ERR_INVAL;

    r = &g_req[id];
    /* 孤児回収中 (requester == ORPHAN のまま残っている表) もここで塞がる。 */
    if (r->phase != LAUNCH_PHASE_IDLE) return OS32_ERR_FULL;

    if (g_next_token >= (i32)LAUNCH_TOKEN_MAX) return OS32_ERR_FULL;

    row_clear(r);
    r->requester = id;
    r->phase = LAUNCH_PHASE_PENDING;
    r->kind = LAUNCH_KIND_LAUNCH;
    r->token = g_next_token++;
    kstrncpy(r->cmdline, cmdline, (u32)LAUNCH_CMDLINE_MAX);
    launch_req_count++;
    return r->token;
}

/* ------------------------------------------------------------------------ */
/*  launch_pending — PENDING の本数 (誰でも)                                 */
/* ------------------------------------------------------------------------ */
i32 launch_pending(void)
{
    int i;
    i32 n = 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        if (g_req[i].phase == LAUNCH_PHASE_PENDING) n++;
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/*  launch_take — PENDING を 1 本取る (owner 1 = WM top-level 専用)          */
/* ------------------------------------------------------------------------ */
i32 launch_take(char *buf, u32 cap, i32 *requester, i32 *kind, i32 *arg)
{
    LaunchReq *r = 0;
    int i;

    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    /* §1a 共通契約「出力ポインタは NULL 可 (書かない)」。buf も出力なので、
     * NULL は断らずに cmdline のコピーだけ飛ばす (KILL の要求のように
     * cmdline が要らない取り方を WM に許す)。cap を見るのは **buf が非 NULL の
     * とき**だけ — NULL に対して cap を問うのは意味がない。 */
    if (buf != 0 && cap < (u32)LAUNCH_CMDLINE_MAX) return OS32_ERR_INVAL;

    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {   /* 要求者 ID 昇順 */
        if (g_req[i].phase == LAUNCH_PHASE_PENDING) { r = &g_req[i]; break; }
    }
    if (r == 0) return 0;                          /* 無し (失敗ではない) */

    r->phase = LAUNCH_PHASE_TAKEN;
    if (buf) kstrncpy(buf, r->cmdline, cap);
    if (requester) *requester = (i32)r->requester;
    if (kind)      *kind = (i32)r->kind;
    if (arg)       *arg = (i32)r->arg;
    return r->token;
}

/* ------------------------------------------------------------------------ */
/*  launch_report — 結果を表へ返す (owner 1 専用)                            */
/* ------------------------------------------------------------------------ */
i32 launch_report(i32 token, i32 rc)
{
    LaunchReq *r;

    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    r = row_by_token(token);
    if (r == 0) return OS32_ERR_STALE;
    /* KILL の正常な順序は「take → exec_kill → 回収通知で DONE → report」で、
     * ここへ来るときには既に TAKEN ではない。WM はこの STALE を再試行せず
     * 正常として扱う (票 §10 non-blocker 2)。 */
    if (r->phase != LAUNCH_PHASE_TAKEN) return OS32_ERR_STALE;

    if (r->kind == LAUNCH_KIND_KILL) {
        /* 正常な順序では、ここへ来る前に child の回収通知が DONE を付けて
         * いる (= 上で STALE を返している)。TAKEN のまま来たということは
         * **子がまだ回収されていない** ので、DONE にしてはいけない —
         * child を落とすと「生きている子の所有が誰の表からも消え、以後の
         * 退場でも回収されない」孤児ができる。ここで消すのは取得済みの印
         * (TAKEN) だけで、表は child を持ったまま RUNNING に戻る。
         * 要求者がもう一度 cancel すれば KILL(child) が再び PENDING になる
         * だけで害はない。DONE を付けるのは常に launch_owner_exit。 */
        r->phase = LAUNCH_PHASE_RUNNING;
        return 0;
    }

    if (rc > 0) {
        /* 生きている非シェル ID でなければ表を壊さずに断る (TAKEN のまま)。 */
        if (!id_ok((int)rc)) return OS32_ERR_INVAL;
        if (appslot_state((int)rc) == APP_STATE_FREE) return OS32_ERR_INVAL;
        r->child = (int)rc;
        r->phase = LAUNCH_PHASE_RUNNING;
        return 0;
    }
    if (rc == 0) {
        /* park より前に終わった短命な子。子 ID は既に回収済み (票 §5 の
         * blocker 1 — 状態の走査では取り逃がす)。 */
        row_finish(r, LAUNCH_PHASE_DONE, 0);
        return 0;
    }
    row_finish(r, LAUNCH_PHASE_FAILED, rc);
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  launch_poll — 要求者だけが自分の要求を読む                               */
/* ------------------------------------------------------------------------ */
i32 launch_poll(i32 token, i32 *status)
{
    LaunchReq *r = row_by_token(token);
    i32 st;

    if (r == 0) return OS32_ERR_STALE;
    if (r->requester != res_owner_get()) return OS32_ERR_INVAL;

    switch (r->phase) {
    case LAUNCH_PHASE_PENDING: st = (i32)LAUNCH_ST_PENDING; break;
    case LAUNCH_PHASE_TAKEN:   st = (i32)LAUNCH_ST_TAKEN;   break;
    case LAUNCH_PHASE_RUNNING: st = (i32)LAUNCH_ST_RUNNING + (i32)r->child; break;
    case LAUNCH_PHASE_DONE:    st = (i32)LAUNCH_ST_DONE;    break;
    default:                   st = (i32)LAUNCH_ST_FAILED + (-(r->rc)); break;
    }
    if (status) *status = st;

    /* 完了を 1 度だけ渡して表を解放する (I5)。 */
    if (r->phase == LAUNCH_PHASE_DONE || r->phase == LAUNCH_PHASE_FAILED) {
        row_clear(r);
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/*  launch_cancel — 要求者だけが取り消す (RUNNING → KILL の PENDING)         */
/* ------------------------------------------------------------------------ */
i32 launch_cancel(i32 token)
{
    LaunchReq *r = row_by_token(token);

    if (r == 0) return OS32_ERR_STALE;
    /* §1a: 「DONE / FAILED / **不一致** → STALE」。要求者が違うのは、別 ID が
     * 他人の token を投げたか、孤児回収中の表を再利用 ID が自分のものと
     * 取り違えたか — どちらも呼び手から見れば「その token はもう自分のもの
     * ではない」で、端末は再試行せずプロンプトへ戻る (D9)。 */
    if (r->requester != res_owner_get()) return OS32_ERR_STALE;

    if (r->phase == LAUNCH_PHASE_RUNNING) {
        /* child は保持したまま「畳んでくれ」に置き換える (I3)。 */
        r->kind = LAUNCH_KIND_KILL;
        r->arg = r->child;
        r->phase = LAUNCH_PHASE_PENDING;
        return 0;
    }
    /* まだ WM に取られていない / 取られて exec_start の途中。端末は次の
     * タイマでもう一度試す (票 D9)。 */
    if (r->phase == LAUNCH_PHASE_PENDING || r->phase == LAUNCH_PHASE_TAKEN) {
        return OS32_ERR_AGAIN;
    }
    return OS32_ERR_STALE;      /* DONE / FAILED */
}

/* ------------------------------------------------------------------------ */
/*  launch_child — その ID の表が所有する子 (誰でも。連鎖の次)               */
/* ------------------------------------------------------------------------ */
i32 launch_child(i32 id)
{
    if (!id_ok((int)id)) return 0;
    return (i32)g_req[(int)id].child;
}

/* ------------------------------------------------------------------------ */
/*  launch_chain — head から末尾まで (票 D8。exec_kill が末尾から畳む)       */
/* ------------------------------------------------------------------------ */
int launch_chain(int head, int *out, int max)
{
    int n = 0;
    int cur = head;
    int i;

    if (out == 0 || max <= 0) return 0;
    while (id_ok(cur) && n < max) {
        for (i = 0; i < n; i++) {      /* 壊れた表が環を作っても止まる */
            if (out[i] == cur) return n;
        }
        out[n++] = cur;
        cur = g_req[cur].child;
    }
    return n;
}

/* ------------------------------------------------------------------------ */
/*  launch_owner_exit — 回収通知 (exec_reclaim_owned から。ID だけを使う)    */
/* ------------------------------------------------------------------------ */
void launch_owner_exit(int id)
{
    LaunchReq *r;
    int i;

    if (!id_ok(id)) return;

    /* (a) この ID を子として持っていた表 — 子は畳まれた。 */
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        r = &g_req[i];
        if (r->phase == LAUNCH_PHASE_IDLE) continue;
        if (r->child != id) continue;
        r->child = 0;                   /* 再利用 ID への誤連鎖を断つ */
        row_finish(r, LAUNCH_PHASE_DONE, 0);
    }

    /* (b) この ID が要求者だった表 — 要求者の退場。 */
    r = &g_req[id];
    if (r->phase != LAUNCH_PHASE_IDLE) {
        if (r->child != 0) {
            /* 孤児回収: カーネルはここから kill しない (回収文脈なので
             * CR3 も段も動かせない)。WM の top-level が take で受けて畳む。 */
            r->requester = LAUNCH_REQ_ORPHAN;
            r->kind = LAUNCH_KIND_KILL;
            r->arg = r->child;
            r->phase = LAUNCH_PHASE_PENDING;
            launch_orphan_count++;
        } else {
            /* 子を持たない要求者の退場も表を解放する (票 §10 1)。 */
            row_clear(r);
        }
    }
}

/* ======================================================================== */
/*  自己診断 (kernel/kselftest.c から。表の遷移を 3 項だけ踏む)              */
/*                                                                          */
/*  実機で壊れたときに見えるのは「sh から外部プログラムが起動しない」か      */
/*  「プロンプトに戻らない」だけで、原因が遠い。ホスト試験                    */
/*  (tools/tests/test_launch.py) と同じ形をブート時にも踏む。                */
/*  res_owner_get() を直に触らずに済むよう、検査は表の素操作だけで組む —     */
/*  launch_req / launch_take の権限は所有者を動かさないと踏めないので、       */
/*  そちらはホスト試験の担当にする。                                          */
/* ======================================================================== */
u32 launch_selftest(void)
{
    u32 bad = 0;
    LaunchReq *r = &g_req[APP_ID_MIN];
    i32 saved_token = g_next_token;
    i32 st = 0;
    int chain[APP_MAX_APPS];
    int n;

    launch_init();

    /* (0) 起動 → 子が付く → 子の回収で DONE + child = 0 */
    row_clear(r);
    r->requester = APP_ID_MIN;
    r->phase = LAUNCH_PHASE_PENDING;
    r->kind = LAUNCH_KIND_LAUNCH;
    r->token = g_next_token++;
    if (launch_pending() != 1) bad |= 1u << 0;
    r->phase = LAUNCH_PHASE_TAKEN;
    r->child = APP_ID_MIN + 1;
    r->phase = LAUNCH_PHASE_RUNNING;
    if (launch_child(APP_ID_MIN) != (i32)(APP_ID_MIN + 1)) bad |= 1u << 0;
    launch_owner_exit(APP_ID_MIN + 1);
    if (r->phase != LAUNCH_PHASE_DONE || r->child != 0) bad |= 1u << 0;
    if (launch_child(APP_ID_MIN) != 0) bad |= 1u << 0;

    /* (1) 完了は 1 度だけ渡り、表は IDLE に戻る (再 poll は STALE) */
    {
        i32 token = r->token;
        int saved_owner_ok = (r->requester == res_owner_get());
        /* poll は要求者からしか通らないので、所有者が違う環境 (ブート時は
         * シェル帯) では権限の負例として見る。 */
        if (saved_owner_ok) {
            if (launch_poll(token, &st) != 0) bad |= 1u << 1;
            if (st != (i32)LAUNCH_ST_DONE) bad |= 1u << 1;
            if (r->phase != LAUNCH_PHASE_IDLE) bad |= 1u << 1;
        } else {
            if (launch_poll(token, &st) != OS32_ERR_INVAL) bad |= 1u << 1;
            row_clear(r);
        }
        if (launch_poll(token, &st) != OS32_ERR_STALE) bad |= 1u << 1;
    }

    /* (2) 要求者の退場は孤児回収 (KILL の PENDING) になり、連鎖が読める */
    launch_init();
    r = &g_req[APP_ID_MIN];
    r->requester = APP_ID_MIN;
    r->phase = LAUNCH_PHASE_RUNNING;
    r->kind = LAUNCH_KIND_LAUNCH;
    r->token = g_next_token++;
    r->child = APP_ID_MIN + 1;
    g_req[APP_ID_MIN + 1].requester = APP_ID_MIN + 1;
    g_req[APP_ID_MIN + 1].phase = LAUNCH_PHASE_RUNNING;
    g_req[APP_ID_MIN + 1].kind = LAUNCH_KIND_LAUNCH;
    g_req[APP_ID_MIN + 1].token = g_next_token++;
    g_req[APP_ID_MIN + 1].child = APP_ID_MIN + 2;
    n = launch_chain(APP_ID_MIN, chain, APP_MAX_APPS);
    if (n != 3 || chain[0] != APP_ID_MIN || chain[2] != APP_ID_MIN + 2) {
        bad |= 1u << 2;
    }
    launch_owner_exit(APP_ID_MIN);
    if (r->phase != LAUNCH_PHASE_PENDING ||
        r->kind != LAUNCH_KIND_KILL ||
        r->arg != APP_ID_MIN + 1 ||
        r->child != APP_ID_MIN + 1 ||
        r->requester != LAUNCH_REQ_ORPHAN) {
        bad |= 1u << 2;
    }
    /* 孤児の完了は poll を待たずに IDLE へ (票 §10 1) */
    launch_owner_exit(APP_ID_MIN + 1);
    if (r->phase != LAUNCH_PHASE_IDLE) bad |= 1u << 2;

    launch_init();
    g_next_token = saved_token;
    return bad;
}
