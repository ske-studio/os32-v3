/* ========================================================================
 *  multiapp_impl_host.c — K5b-K の「実装」を K5a の模型と同じ検査で回す
 *
 *  対象票: docs/archive/gui_v13/TASK_K5B_kernel.md (ホスト試験の 3 本目)
 *  実行:   python3 -B tools/tests/test_multiapp_impl.py
 *  記録:   tools/tests/k5b_kernel_tdd.md
 *
 *  K5a の tools/tests/multiapp_model_host.c は「設計の模型」で、状態機械を
 *  その場に手書きしていた。こちらは **実物の AppSlot 管理コード**
 *  (exec/appslot.c) をそのままコンパイルして、同じ 84 検査を回す。
 *  番号と検査名は模型と 1 対 1 で対応させてあるので、落ちた検査名で
 *  「模型のどの規則が実装で崩れたか」がそのまま分かる。
 *
 *  模型との差 (意図したもの、報告済み):
 *    (a) 戻り値は模型の MA_ERR_* ではなく **実物の OS32_ERR_* / EXEC_ERR_***。
 *        検査名は同じで、期待値だけ実物の定数に読み替えてある。
 *    (b) ケース 6 (SHM スロット) と 12〜16 (同時 ready の選択規則) は
 *        設計 D6 / D11-5 が **gshell (WM) の領分**と決めた部分なので、
 *        カーネルには無い。ハーネス側に模型と同じ規則を置き、その下で
 *        park / resume だけを実物に通す (= 規則と機構の噛み合わせを見る)。
 *        W レーンはこの規則を Rust へ写す。
 *    (c) ケース 17 は票 K5b の追加要求 = 「印なし resume の拒否」(C6) の負例。
 *
 *  C89 ([C1])。libc は使わない (-nostdlib で直接走る)。
 * ======================================================================== */

#include "types.h"

/* ---- 実物のカーネルコード (ハードウェアには一切触らない部分) ---------- */
/* T9 §12 T1: 標準 FD のリダイレクト表も **実物** (fs/fd_redirect.c) を
 * そのまま取り込む。park / resume で ID ごとに持ち替わることを、模型では
 * なく実物の表で見るため。res_owner_set/get の実体もこちらにあるので、
 * ハーネス側の写しは持たない (所有者タグが本物になる)。
 * VFS はこの票の対象外なので、ファイル FD だけ最小の偽物を置く —
 * 見たいのは「どの表に書き込みが入ったか」と「閉じたか」だけ。 */
#include "vfs.h"
#include "fd_redirect.h"   /* res_owner_get (偽 vfs_open の owner タグ用) */

#define HOST_VFS_MAX_FD 8
static int  host_fd_open[HOST_VFS_MAX_FD];
static int  host_fd_owner[HOST_VFS_MAX_FD];   /* vfs_fd.c の owner タグ相当 */
static u32  host_fd_written[HOST_VFS_MAX_FD];
static int  host_fd_closes;                   /* 実際に閉じた回数 */
static int  host_fd_close_calls[HOST_VFS_MAX_FD];  /* FD ごとの vfs_close 呼び出し */
static int  host_next_fd;

int vfs_open(const char *path, int mode)
{
    (void)path; (void)mode;
    if (host_next_fd >= HOST_VFS_MAX_FD) return -1;
    host_fd_open[host_next_fd] = 1;
    /* 実物の vfs_fd.c と同じく、開いた時点の所有者で FD をタグ付けする。
     * リダイレクトの file_fd もこれで **その ID のもの**になるので、
     * 回収は vfs_close_owned(id) が担う (票 §12 T1、往復 9 の指摘)。 */
    host_fd_owner[host_next_fd] = res_owner_get();
    host_fd_written[host_next_fd] = 0;
    return host_next_fd++;
}

void vfs_close(int fd)
{
    if (fd < 0 || fd >= HOST_VFS_MAX_FD) return;
    /* **呼ばれた回数**と**実際に閉じた回数**を別に数える。二重 close は
     * 「閉じた回数」には出ない (2 回目は in_use が落ちている) ので、
     * 呼び出し回数で見ないと往復 9 の指摘が捕まえられない。 */
    host_fd_close_calls[fd]++;
    if (host_fd_open[fd]) host_fd_closes++;
    host_fd_open[fd] = 0;
}

/* fs/vfs_fd.c の vfs_close_owned の偽物 (exec_reclaim_owned の (2))。
 * 実物と同じく「その owner が開いた、まだ開いている FD」を閉じる。 */
static void host_vfs_close_owned(int owner)
{
    int fd;
    for (fd = 0; fd < HOST_VFS_MAX_FD; fd++) {
        if (!host_fd_open[fd]) continue;
        if (host_fd_owner[fd] != owner) continue;
        vfs_close(fd);
    }
}

int vfs_seek(int fd, int offset, int whence)
{
    (void)fd; (void)offset; (void)whence;
    return 0;
}

int vfs_read_fd(int fd, void *buf, u32 size)
{
    (void)fd; (void)buf; (void)size;
    return 0;
}

int vfs_write_fd(int fd, const void *buf, u32 size)
{
    (void)buf;
    if (fd < 0 || fd >= HOST_VFS_MAX_FD || !host_fd_open[fd]) return -1;
    host_fd_written[fd] += size;
    return (int)size;
}

#include "fd_redirect.c"
#include "redir_access_stub.h"

/* fd_redirect.c の書き込み時の再検査 (票 TASK_KAPI_OUTPUT_GUARD、実装レビュー 4)
 * が引く exec/exec.c の 5 本。ホストではユーザ帯の番地は無いので
 * ring3_ptr_ok は常に 0 (= 再検査を通らない)。呼ばれたら落ちる側は数える。 */
static int host_fault_kills = 0;
int ring3_ptr_ok(u32 p) { (void)p; return 0; }
int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb)
{ (void)pa; (void)la; (void)pb; (void)lb; return 1; }
/* 由来つきの門 (2026-09-26): ここの呼び手は CPL=0 扱い (由来はアプリでない)。
 * アプリ由来の筋書きは tools/tests/ring3_guard_host.c が見る。 */
int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb)
{ (void)pa; (void)la; (void)pb; (void)lb; return 1; }
int ring3_call_from_user(void) { return 0; }
void ring3_fault_kill(void) { host_fault_kills++; for (;;) { } }

#include "appslot.c"

/* K7: 注入リングは **実物** (kernel/kbd_inject.c) をそのまま取り込む。
 * 権限の照合相手だけハーネスが持つ (con_sink はここでは要らない —
 * 「読み手だけが注げる」は tools/tests/kbd_inject_host.c が本物の
 * kernel/con_sink.c と組んで見ている)。 */
static int host_reader = 2;
int con_sink_reader_get(void) { return host_reader; }
volatile int ring3_wm_depth;
#include "kbd_inject.c"

/* T9: 起動要求表も **実物** (exec/launch.c) をそのまま取り込む。要るのは
 * GUI 判定と kstrncpy だけ (表そのものの検査は tools/tests/launch_host.c)。
 * ここで見るのは exec_kill の連鎖 (D8) と sys_yield の resume (D5) — 表と
 * AppSlot が噛み合う所。 */
static int host_gui = 1;
int con_sink_is_enabled(void) { return host_gui; }
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    while (i + 1 < n && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return dst;
}
#include "launch.c"

/* ---- 試験ハーネス ----------------------------------------------------- */

#define MA_SLOT_MAX   4              /* = include/memmap.h の GUI_SLOT_MAX */

#define MA_RES_FD     0
#define MA_RES_PIPE   1
#define MA_RES_WINDOW 2
#define MA_RES_TIMER  3
#define MA_RES_SHM    4
#define MA_RES_KINDS  5

#define MA_OP_WAIT   1
#define MA_OP_POLL   2
#define MA_OP_COMMIT 3

/* D11 の 2 つの上限 (WM 側の私有状態。模型と同じ値・同じ式)。 */
#define MA_INPUT_STREAK_MAX  APP_MAX_APPS
#define MA_STARVE_BOUND \
    ((2 * APP_MAX_APPS - 2) * (MA_INPUT_STREAK_MAX + 1))

/* 実物では pgalloc / gshell が持つもの。ハーネスが代わりに持つ。 */
typedef struct {
    int  slot[APP_SLOT_COUNT];        /* SHM スロット (gshell の alloc_slot) */
    int  res[APP_SLOT_COUNT][MA_RES_KINDS];
    int  input_ready[APP_SLOT_COUNT];
    int  derived_ready[APP_SLOT_COUNT];
    int  turn_used[APP_SLOT_COUNT];
    u32  free_pages;
    int  cpl0_children;               /* exec/exec.c の g_cpl0_children */
    int  last_run;
    int  focus;
    int  input_streak;
    int  exit_status;
} MaHost;

static MaHost H;

static void ma_init(u32 free_pages)
{
    int i, k;
    appslot_init();
    ring3_switch_count = 0;
    ring3_transition_count = 0;
    ring3_park_reject_count = 0;
    ring3_resume_bad_frame_count = 0;
    appslot_reclaim_count = 0;
    appslot_last_reclaim_id = 0;
    for (i = 0; i < APP_SLOT_COUNT; i++) {
        H.slot[i] = -1;
        H.input_ready[i] = 0;
        H.derived_ready[i] = 0;
        H.turn_used[i] = 0;
        for (k = 0; k < MA_RES_KINDS; k++) H.res[i][k] = 0;
    }
    H.free_pages = free_pages;
    H.cpl0_children = 0;
    H.last_run = 0;
    H.focus = 0;
    H.input_streak = 0;
    H.exit_status = 0;
}

/* 模型の pd_switches = 「アプリ PD を CR3 に載せた回数」。実物は受入 G7 の
 * ために 2 本に割ってあるので (D0)、模型と突き合わせるときは足す。 */
static u32 ma_pd_switches(void)
{
    return ring3_switch_count + ring3_transition_count;
}

/* gshell の alloc_slot (wm.rs:641) と同じ規則。小さい順、ID と 1 対 1。 */
static int ma_alloc_slot(void)
{
    int s, i, taken;
    for (s = 0; s < MA_SLOT_MAX; s++) {
        taken = 0;
        for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
            if (appslot_get(i) != 0 && H.slot[i] == s) taken = 1;
        }
        if (!taken) return s;
    }
    return -1;
}

static int ma_start(u32 pages, int gui)
{
    int id = appslot_start_admit(gui, pages, H.free_pages);
    int slot = -1;
    if (id < 0) return id;
    if (gui) {
        slot = ma_alloc_slot();
        if (slot < 0) return OS32_ERR_FULL;
    }
    appslot_start_commit(id, gui, pages);
    H.free_pages -= pages;
    H.slot[id] = slot;
    H.turn_used[id] = 1;
    H.last_run = id;
    H.input_streak = 0;
    return id;
}

/* exec_launch が起動時にスロットへ書く 2 つ (ctx->cpl3 = 1 / ctx->hdr_flags =
 * hdr->flags) を、立てたあとに写す。票 T8 の判定材料はこの 2 つだけ。 */
static int ma_start_gfx(u32 pages, int gui, u32 hdr_flags)
{
    int id = ma_start(pages, gui);
    AppSlot *a;
    if (id < 0) return id;
    a = appslot_at(id);
    a->cpl3 = 1;
    a->hdr_flags = hdr_flags;
    return id;
}

/* ---- CUI 専用の宣言 (票 T8-2) — exec_launch の順番のまま ----------------
 *   池の admit (状態は変えない) → ヘッダ読み → cui_only の admit → 起動。
 * 拒否は池の消費より後・commit より前でなければならない (池も帯も動かさない
 * こと)。宣言以外は普通の CPL=3 アプリなので --cpl0 の枝は通らない。 */
static int ma_start_cui_only(int gui)
{
    int id = appslot_start_admit(gui, 0, 0);     /* 池だけ。状態は変えない */
    AppSlot *a;
    if (id < 0) return id;
    if (appslot_cui_only_admit(gui, OS32X_FLAG_CUI_ONLY) < 0)
        return OS32_ERR_INVAL;
    appslot_start_commit(id, gui, 0);
    a = appslot_at(id);
    a->cpl3 = 1;
    a->hdr_flags = OS32X_FLAG_CUI_ONLY;
    H.turn_used[id] = 1;
    H.last_run = id;
    return id;
}

static void ma_gui_call(int op) { appslot_gui_op_enter(op == MA_OP_WAIT); }
static void ma_gui_return(void) { appslot_gui_op_leave(); }

static int ma_park(void)
{
    int rc = appslot_park_check();
    if (rc < 0) return rc;
    appslot_park_commit();
    return 0;
}

static int ma_resume(int id)
{
    int rc = appslot_resume_check(id);
    if (rc < 0) return rc;
    appslot_resume_commit(id);
    H.turn_used[id] = 1;
    H.last_run = id;
    H.input_streak = 0;
    return 0;
}

/* ---- K7: 第 2 の park 点 (GUI 中の kbd 待ち) --------------------------- */
/* 表 (appslot.c) は実物。CR3 / フレーム / longjmp を持つ exec/exec.c は
 * ホストに持ち込めないので、exec_park_kbd / exec_resume の **kbd 分岐だけ**
 * をここに写す (ケース 12〜16 が gshell の選択規則を写しているのと同じ扱い)。
 * 写した部分は 3 行 — 印を見て注入リングから 1 バイト取り、EAX に入れ、
 * 空なら OS32_ERR_AGAIN で起こさない (票 §5 の指摘 B)。 */
static int ma_park_kbd(void)
{
    int rc = appslot_park_kbd_check();
    if (rc < 0) return rc;
    appslot_park_kbd_commit();
    return 0;
}

static int ma_resume_kbd(int id)
{
    AppSlot *a;
    u8 ch;
    int rc = appslot_resume_check(id);
    if (rc < 0) return rc;
    a = appslot_get(id);
    if (a->parked_from_kbd) {
        i32 app_id = id;
    (void)app_id;
#include "exec_resume_kbd.inc"
    } else {
        a->frame[APP_FRAME_EAX] = 0;
    }
    appslot_resume_commit(id);
    H.turn_used[id] = 1;
    H.last_run = id;
    return 0;
}

/* ---- T8 §7 D8: 第 3 の park 点 (GUI 中のポーリング型の協調 yield) ------ */
/* K7 の 2 本と同じ扱い。表 (appslot.c) は実物で、exec/exec.c の
 * exec_park_poll / exec_resume の **poll 分岐だけ** をここに写す。写した分は
 * 3 行 — 印を見て注入リングから 1 バイト取り、空なら **-1 (キーなし)** を
 * EAX に入れる (WAIT_KEY と違い、空でも起こす)。
 * now_tick は drivers/kbd.c が tick_count から渡す値の差し替え。 */
static int ma_park_poll(u32 now_tick)
{
    int rc = appslot_park_poll_check(now_tick);
    if (rc < 0) return rc;
    appslot_park_poll_commit();
    return 0;
}

static int ma_resume_poll(int id)
{
    i32 app_id = id;
    (void)app_id;
    AppSlot *a;
    u8 ch;
    int rc = appslot_resume_check(id);
    if (rc < 0) return rc;
    a = appslot_get(id);
    /* exec/exec.c の exec_resume と同じく、EAX の出所は印から導く。
     * 明示的な譲り (T9 D5) は注入リングを読まず 0 を入れる。 */
    switch (appslot_resume_source(id)) {
    case APP_RESUME_SRC_YIELD:
        a->frame[APP_FRAME_EAX] = 0;
        break;
    case APP_RESUME_SRC_POLL:
#include "exec_resume_poll.inc"
        break;
    default:
        a->frame[APP_FRAME_EAX] = 0;
        break;
    }
    appslot_resume_commit(id);
    H.turn_used[id] = 1;
    H.last_run = id;
    return 0;
}

static int ma_res_add(int kind, int n)
{
    int owner = res_owner_get();
    if (kind < 0 || kind >= MA_RES_KINDS) return OS32_ERR_INVAL;
    if (owner < APP_ID_MIN || owner > APP_ID_MAX) return OS32_ERR_INVAL;
    H.res[owner][kind] += n;
    return 0;
}

/* 実物の回収の並び (exec_exit): fd_redirect_reset_owned / vfs_close_owned /
 * pipe_free_owned / shm_free_owned / db_cleanup_owned / gui_owner_exit —
 * 全部この 1 つの ID で呼ぶ。ハーネスは「その ID の分だけ消える」を数える。
 * 票 T8 D1 で (11) 画面の所有者が並びの末尾に加わった。こちらは実物を呼ぶ。 */
static void ma_reclaim_res(int id)
{
    int k;
    /* exec/exec.c の exec_reclaim_owned (1)。いまの表からその ID のものを外す。 */
    fd_redirect_reset_owned(id);
    /* exec/exec.c の exec_reclaim_owned (2) vfs_close_owned。park したまま
     * 畳まれた ID のリダイレクトの file_fd は「いまの表」に無いが、FD 自体は
     * その ID の owner タグを持っているのでここで閉じる (票 §12 T1)。
     * appslot_reclaim は枠を **空にするだけ** — 閉じると二重 close になる。 */
    host_vfs_close_owned(id);
    /* exec/exec.c の exec_reclaim_owned (9b)。ID だけを使う (票 T9 D3)。 */
    launch_owner_exit(id);
    appslot_gfx_owner_exit(id);
    for (k = 0; k < MA_RES_KINDS; k++) H.res[id][k] = 0;
    H.slot[id] = -1;
    H.input_ready[id] = 0;
    H.derived_ready[id] = 0;
    H.turn_used[id] = 0;
}

static int ma_exit(int status)
{
    int id = appslot_cur();
    int target;
    if (id < APP_ID_MIN || id > APP_ID_MAX) return OS32_ERR_INVAL;
    H.exit_status = status;
    target = appslot_return_target(id);
    ma_reclaim_res(id);
    H.free_pages += appslot_reclaim(id);
    appslot_switch_to(target);
    return 0;
}



static int ma_fault(void) { return ma_exit(-1); }

static int ma_abort_request(void)
{
    return appslot_abort_request() ? 0 : OS32_ERR_INVAL;
}

/* exec/exec.c の ring3_abort_request (IRQ1 の ISR から呼ばれる) を**そのまま
 * 写した**形 (exec.c はカーネル一式を引くのでホストへ #include できない)。
 * GUI 判定 (con_sink_is_enabled) と tick (tick_count) は呼び出し側が渡す。
 * 上の ma_abort_request はこの関門を通らない直呼び = gfx 拒否 (T8 D1a) と
 * V86 の脱出の経路。 */
/* exec/exec.c の ring3_syscall_dispatch の**入口の 1 行**を写した形
 * (票 §12 S6b)。走っているアプリが KAPI を 1 本呼んだ = カーネルへ入った。
 * 実物は g_cur_app (CPL=3 で走っているスロット) を見るので、ここも
 * アプリ ID (2〜5) のときだけ控える。 */
static void ma_syscall_enter(u32 now_tick)
{
    AppSlot *a;
    if (appslot_cur() < APP_ID_MIN) return;
    a = appslot_get(appslot_cur());
    if (a) a->last_kernel_tick = now_tick;
}

static int ma_irq_abort_request(int gui_mode, u32 now_tick)
{
    if (!appslot_abort_admit(gui_mode, now_tick)) return 0;
    return appslot_abort_request();
}

/* KAPI v45 exec_abort_clear の実体 (決裁 A1)。owner は呼ぶ側の文脈のまま。 */
static int ma_abort_clear(void)
{
    return appslot_abort_clear();
}

static int ma_abort_check(void)
{
    AppSlot *a = appslot_get(appslot_cur());
    if (!a) return OS32_ERR_INVAL;
    if (!a->abort_req) return 0;
    return ma_exit(-2);
}

static int ma_kill(int id)
{
    int rc = appslot_kill_check(id);
    if (rc < 0) return rc;
    ma_reclaim_res(id);
    H.free_pages += appslot_reclaim(id);
    return 0;
}

/* 第 4 の park 点 (票 T9 D5)。exec/exec.c の exec_sys_yield の表の部分。 */
static int ma_park_yield(void)
{
    int rc = appslot_park_yield_check();
    if (rc < 0) return rc;
    appslot_park_yield_commit();
    return 0;
}

/* exec/exec.c の exec_kill (票 T9 D8) の**連鎖の部分をそのまま写した**形
 * (exec.c はカーネル一式を引くのでホストへ #include できない)。畳む 1 本分は
 * 既存の ma_kill と同じ手順 (回収 → スロット返却)。順番が効く: 末尾から
 * 畳まないと、各段の回収通知が親の表を DONE にする前に親が消える。 */
static int ma_kill_order[APP_SLOT_COUNT];
static int ma_kill_order_n;

static int ma_kill_chain(int head)
{
    int chain[APP_MAX_APPS];
    int n, i;
    int rc = appslot_kill_check(head);
    if (rc < 0) return rc;
    ma_kill_order_n = 0;
    n = launch_chain(head, chain, APP_MAX_APPS);
    if (n <= 0) {
        ma_kill_order[ma_kill_order_n++] = head;
        ma_reclaim_res(head);
        H.free_pages += appslot_reclaim(head);
        return 0;
    }
    for (i = n - 1; i >= 0; i--) {
        if (chain[i] != head && appslot_kill_check(chain[i]) < 0) continue;
        ma_kill_order[ma_kill_order_n++] = chain[i];
        ma_reclaim_res(chain[i]);
        H.free_pages += appslot_reclaim(chain[i]);
    }
    return 0;
}

/* ---- D11: 同時 ready の選択規則 (WM 側。D11-5 でカーネルには置かない) --- */
static void ma_set_ready(int id, int input, int derived)
{
    if (id < APP_ID_MIN || id > APP_ID_MAX) return;
    H.input_ready[id] = input;
    H.derived_ready[id] = derived;
}

static int ma_round_remaining(void)
{
    int i, n = 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        AppSlot *a = appslot_get(i);
        if (a && a->state == APP_STATE_PARKED && !H.turn_used[i] &&
            (H.input_ready[i] || H.derived_ready[i])) n++;
    }
    return n;
}

static int ma_pick_group(int want_input)
{
    int start, n, i, id;
    start = (H.last_run >= APP_ID_MIN && H.last_run <= APP_ID_MAX) ?
            (H.last_run - APP_ID_MIN + 1) : 0;
    for (n = 0; n < APP_MAX_APPS; n++) {
        AppSlot *a;
        i = (start + n) % APP_MAX_APPS;
        id = APP_ID_MIN + i;
        a = appslot_get(id);
        if (!a || a->state != APP_STATE_PARKED || H.turn_used[id]) continue;
        if (want_input) {
            if (H.input_ready[id]) return id;
        } else {
            if (!H.input_ready[id] && H.derived_ready[id]) return id;
        }
    }
    return 0;
}

static int ma_pick(void)
{
    int f, k, i;
    if (appslot_cur() != APP_ID_SHELL) return 0;
    if (ma_round_remaining() == 0) {
        for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) H.turn_used[i] = 0;
        if (ma_round_remaining() == 0) return 0;
    }
    f = H.focus;
    if (f >= APP_ID_MIN && f <= APP_ID_MAX) {
        AppSlot *a = appslot_get(f);
        if (a && a->state == APP_STATE_PARKED && !H.turn_used[f] &&
            H.input_ready[f]) return f;
    }
    k = ma_pick_group(1);
    if (k) return k;
    return ma_pick_group(0);
}

static int ma_should_park(void)
{
    int cur = appslot_cur();
    int i, other_ready = 0;
    AppSlot *a;
    if (cur < APP_ID_MIN || cur > APP_ID_MAX) return 0;
    a = appslot_get(cur);
    if (!a || a->state != APP_STATE_RUNNING) return 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) {
        AppSlot *o = appslot_get(i);
        if (o && o->state == APP_STATE_PARKED &&
            (H.input_ready[i] || H.derived_ready[i])) other_ready = 1;
    }
    if (!other_ready) return 0;
    if (H.input_ready[cur] && H.input_streak < MA_INPUT_STREAK_MAX) {
        H.input_streak++;
        return 0;
    }
    return 1;
}

/* ---- 出力 ------------------------------------------------------------- */
static void die(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    for (;;) {}
}

static void report(const char *text)
{
    u32 len = 0;
    while (text[len]) len++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len)
                     : "memory");
}

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    if (cond) {
        report("  ok   ");
    } else {
        report("  FAIL ");
        failures++;
    }
    report(name);
    report("\n");
}

static void fill_four(u32 pages_each)
{
    int i;
    for (i = 0; i < 4; i++) {
        ma_start(pages_each, 1);
        ma_gui_call(MA_OP_WAIT);
        ma_park();
    }
}

/* ---- 1. 5 本目は ERR_FULL、既存 4 本は無事 (票 G3) ---- */
static void case_fifth_refused(void)
{
    int ids[4], i, r;
    ma_init(4096);
    for (i = 0; i < 4; i++) {
        ids[i] = ma_start(100, 1);
        ma_gui_call(MA_OP_WAIT);
        ma_park();
    }
    check(ids[0] == 2 && ids[1] == 3 && ids[2] == 4 && ids[3] == 5,
          "1a ID は 2..5 が小さい順に配られる");
    r = ma_start(100, 1);
    check(r == OS32_ERR_FULL, "1b 5 本目は ERR_FULL");
    check(appslot_live() == 4, "1c 5 本目の拒否で既存 4 本は減らない");
    check(H.free_pages == 4096 - 400,
          "1d 拒否された 5 本目はページを 1 枚も取らない");
    check(appslot_cur() == APP_ID_SHELL && res_owner_get() == APP_ID_SHELL,
          "1e 拒否のあと WM top-level のまま");
}

/* ---- 2. 終了は 1 本分だけ回収する (票 G2) ---- */
static void case_exit_reclaims_one(void)
{
    ma_init(4096);
    fill_four(100);
    ma_resume(2);
    ma_res_add(MA_RES_FD, 3);
    ma_res_add(MA_RES_WINDOW, 2);
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    ma_resume(3);
    ma_res_add(MA_RES_FD, 5);
    ma_res_add(MA_RES_TIMER, 1);
    ma_exit(0);

    check(appslot_live() == 3, "2a 終了で生きている本数が 1 本だけ減る");
    check(appslot_reclaim_count == 1 && appslot_last_reclaim_id == 3,
          "2b 回収は終了した ID に対して 1 回だけ");
    check(appslot_last_reclaim_id == 3,
          "2c gui_owner_exit は終了した ID で呼ばれる");
    check(H.free_pages == 4096 - 300, "2d ページは 1 本分だけ返る");
    check(appslot_get(2) != 0 && H.res[2][MA_RES_FD] == 3 &&
          H.res[2][MA_RES_WINDOW] == 2,
          "2e 他のアプリの資源は 1 つも触られない");
    check(appslot_get(3) == 0, "2f 終了した ID は空く");
    check(appslot_cur() == APP_ID_SHELL, "2g 終了後は WM top-level へ戻る");
}

/* ---- 3. 切替は OP_WAIT の中でだけ (票 G7 / 契約 T2a) ---- */
static void case_switch_only_in_op_wait(void)
{
    u32 before;
    ma_init(4096);
    ma_start(100, 1);
    check(ma_park() < 0, "3a gui_call の外では park できない");
    ma_gui_call(MA_OP_POLL);
    check(ma_park() < 0, "3b OP_POLL の中では park できない");
    ma_gui_return();
    ma_gui_call(MA_OP_COMMIT);
    check(ma_park() < 0, "3c OP_COMMIT の中では park できない");
    ma_gui_return();
    before = ma_pd_switches();
    check(ma_pd_switches() == before, "3d park に失敗した間 CR3 は動かない");
    ma_gui_call(MA_OP_WAIT);
    check(ma_park() == 0, "3e OP_WAIT の中でだけ park できる");
    check(res_owner_get() == APP_ID_SHELL, "3f park で owner は 1 (WM) に戻る");
    check(ma_resume(2) == 0, "3g top-level から resume できる");
    check(ma_pd_switches() == before + 1,
          "3h CR3 が動くのは resume の 1 回だけ");
}

/* ---- 4. 走っているアプリの横取りは起きない ---- */
static void case_no_preemption(void)
{
    ma_init(4096);
    fill_four(100);
    ma_resume(2);
    check(ma_resume(3) == OS32_ERR_INVAL,
          "4a アプリが走っている間は別のアプリを起こせない");
    check(appslot_cur() == 2, "4b 横取りが弾かれても走っているのは同じ 1 本");
    check(ma_kill(3) == OS32_ERR_INVAL,
          "4c アプリが走っている間は WM の kill も走らない");
}

/* ---- 5. fault は他のアプリの資源に触らない (票 G5) ---- */
static void case_fault_isolated(void)
{
    int i;
    ma_init(4096);
    fill_four(100);
    for (i = 2; i <= 5; i++) {
        ma_resume(i);
        ma_res_add(MA_RES_FD, i);
        ma_res_add(MA_RES_SHM, 1);
        ma_gui_call(MA_OP_WAIT);
        ma_park();
    }
    ma_resume(3);
    check(ma_fault() == 0, "5a 走っているアプリは fault で畳める");
    check(appslot_live() == 3, "5b fault で減るのは 1 本だけ");
    check(appslot_get(2) != 0 && H.res[2][MA_RES_FD] == 2 &&
          H.res[2][MA_RES_SHM] == 1,
          "5c fault の前後で ID 2 の資源は不変");
    check(appslot_get(4) != 0 && H.res[4][MA_RES_FD] == 4 &&
          H.res[4][MA_RES_SHM] == 1,
          "5d fault の前後で ID 4 の資源は不変");
    check(H.free_pages == 4096 - 300, "5e 返るページは 1 本分だけ");
}

/* ---- 6. スロットと ID の対応は一意で、park/resume で動かない ---- */
static void case_slot_bijection(void)
{
    int i, j, s[4], id;
    ma_init(4096);
    fill_four(100);
    for (i = 0; i < 4; i++) s[i] = H.slot[APP_ID_MIN + i];
    check(s[0] == 0 && s[1] == 1 && s[2] == 2 && s[3] == 3,
          "6a スロットは 0..3 が小さい順に配られる");
    for (i = 0; i < 4; i++) {
        for (j = i + 1; j < 4; j++) {
            if (s[i] == s[j]) { check(0, "6b スロットが重複しない"); return; }
        }
    }
    check(1, "6b スロットが重複しない");
    ma_resume(4);
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    check(H.slot[4] == 2, "6c park/resume でスロットは動かない");
    ma_resume(3);
    ma_exit(0);
    id = ma_start(100, 1);
    check(id == 3 && H.slot[3] == 1,
          "6d 空いた ID とスロットが次の起動へ回る");
}

/* ---- 7. メモリが足りなければ拒否する (D5、スワップしない) ---- */
static void case_nomem_refuses(void)
{
    int r;
    ma_init(250);
    ma_start(100, 1);
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    ma_start(100, 1);
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    r = ma_start(100, 1);
    check(r == EXEC_ERR_NOMEM, "7a 入らない要求は ERR_NOMEM で拒否");
    check(appslot_live() == 2, "7b 拒否で既存のアプリは減らない");
    check(H.free_pages == 50, "7c 拒否は空きページを 1 枚も動かさない");
    check(ma_start(50, 1) == 4, "7d 入る大きさなら 3 本目も起動できる");
}

/* ---- 8. CUI 互換: 入れ子 exec_run では 段 = ID ---- */
static void case_cui_nesting(void)
{
    int a, b, c;
    ma_init(4096);
    a = ma_start(10, 0);
    b = ma_start(10, 0);
    c = ma_start(10, 0);
    check(a == 2 && b == 3 && c == 4,
          "8a 入れ子 exec の ID は段の深さと同じ (2,3,4)");
    check(H.slot[2] == -1 && H.slot[4] == -1,
          "8b CUI の子は GUI スロットを取らない");
    check(res_owner_get() == 4, "8c 走っているのは最も深い段");
    ma_exit(0);
    check(appslot_cur() == 3 && res_owner_get() == 3,
          "8d 終了で 1 段だけ親へ戻る");
    ma_exit(0);
    check(appslot_cur() == 2 && res_owner_get() == 2, "8e さらに 1 段戻る");
    ma_exit(0);
    check(appslot_cur() == APP_ID_SHELL && res_owner_get() == APP_ID_SHELL,
          "8f 最後はシェル (ID 1) へ戻る");
    check(appslot_live() == 0 && H.free_pages == 4096,
          "8g 3 段ぶんのページが全部返る");
    fill_four(10);
    check(ma_start(10, 0) == OS32_ERR_FULL,
          "8h GUI 4 本のときは CUI の入れ子も ERR_FULL");
}

/* ---- 9. CTRL+STOP は走っているアプリ宛にしか立たない (票 G4) ---- */
static void case_abort_targets_running(void)
{
    ma_init(4096);
    fill_four(100);
    ma_resume(3);
    ma_abort_request();
    check(appslot_get(2) != 0 && appslot_get(2)->abort_req == 0,
          "9a 止めてあるアプリに CTRL+STOP は立たない");
    check(appslot_get(3) != 0 && appslot_get(3)->abort_req == 1,
          "9b 走っているアプリにだけ立つ");
    check(ma_abort_check() == 0, "9c 次の安全地点で畳まれる");
    check(appslot_live() == 3 && appslot_get(3) == 0,
          "9d 畳まれるのはその 1 本だけ");
    check(appslot_cur() == APP_ID_SHELL, "9e 畳んだあと WM top-level へ戻る");
    check(ma_kill(4) == 0, "9f 止めてあるアプリは kill で畳める");
    check(appslot_live() == 2, "9g kill も 1 本だけ");
}

/* ---- 10. 起動失敗はどの状態も動かさない ---- */
static void case_failed_start_is_clean(void)
{
    u32 sw;
    ma_init(4096);
    fill_four(100);
    sw = ma_pd_switches();
    check(ma_start(100, 1) == OS32_ERR_FULL, "10a 5 本目は起動しない");
    check(ma_pd_switches() == sw, "10b 起動失敗では CR3 を載せ替えない");
    check(res_owner_get() == APP_ID_SHELL, "10c 起動失敗で owner は 1 のまま");
    check(appslot_reclaim_count == 0, "10d 起動失敗では回収を回さない");
}

/* ---- 11. GUI の起動は WM top-level からだけ (契約 S2) ---- */
static void case_gui_start_only_from_toplevel(void)
{
    ma_init(4096);
    ma_start(100, 1);
    check(ma_start(100, 1) == OS32_ERR_INVAL,
          "11a 走っているアプリの中から GUI アプリは起動できない");
    check(appslot_live() == 1, "11b 弾かれた起動は本数を増やさない");
    check(H.free_pages == 4096 - 100, "11c 弾かれた起動はページを取らない");
    check(appslot_cur() == 2, "11d 弾かれても走っているのは元のアプリのまま");
    check(ma_start(10, 0) == 3, "11e CUI の入れ子 exec_run は従来どおり通る");
}

/* ---- 12. 同時 ready の選択規則 (D11) ---- */
static void case_pick_rule(void)
{
    ma_init(4096);
    fill_four(100);

    ma_set_ready(3, 1, 0);
    ma_set_ready(5, 1, 0);
    H.focus = 5;
    H.last_run = 2;
    check(ma_pick() == 5, "12a 入力群にフォーカスが居ればフォーカスを選ぶ");

    H.focus = 2;
    H.last_run = 3;
    check(ma_pick() == 5, "12b 入力群はラウンドロビン (last_run の次から)");

    ma_set_ready(3, 0, 0);
    ma_set_ready(5, 0, 0);
    ma_set_ready(2, 0, 1);
    ma_set_ready(4, 0, 1);
    H.focus = 2;
    H.last_run = 2;
    check(ma_pick() == 4, "12c 導出群もラウンドロビン");
    H.focus = 4;
    H.last_run = 4;
    check(ma_pick() == 2, "12d 導出群ではフォーカスを優先しない");

    ma_set_ready(5, 1, 0);
    H.focus = 2;
    H.last_run = 4;
    check(ma_pick() == 5, "12e 入力群は導出群より先");

    ma_set_ready(2, 0, 0);
    ma_set_ready(4, 0, 0);
    ma_set_ready(5, 0, 0);
    check(ma_pick() == 0, "12f ready が 1 本も無ければ誰も起こさない");
}

/* ---- 13. 走っているアプリが park すべきか (D11 (1)) ---- */
static void case_park_decision(void)
{
    ma_init(4096);
    fill_four(100);
    ma_resume(2);
    ma_gui_call(MA_OP_WAIT);

    ma_set_ready(2, 1, 0);
    ma_set_ready(4, 1, 0);
    check(ma_should_park() == 0, "13a 自分に入力があれば park しない");

    ma_set_ready(2, 0, 1);
    ma_set_ready(4, 0, 0);
    check(ma_should_park() == 0,
          "13b 他に ready が居なければ park しない (1 本のときの回帰ゼロ)");

    ma_set_ready(4, 1, 0);
    check(ma_should_park() == 1, "13c 他に入力があれば導出だけの自分は譲る");

    ma_set_ready(4, 0, 1);
    check(ma_should_park() == 1, "13d 他も導出だけなら巡回のため譲る");

    ma_set_ready(2, 0, 0);
    check(ma_should_park() == 1, "13e 自分が ready でなければ譲る");
}

/* ---- 14. 飢餓が起きない (導出群だけの 4 本が 1 周で全員走る) ---- */
static void case_no_starvation(void)
{
    int seen[APP_MAX_APPS];
    int order[4];
    int i, k;
    ma_init(4096);
    fill_four(100);
    for (i = 0; i < APP_MAX_APPS; i++) seen[i] = 0;
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) ma_set_ready(i, 0, 1);
    H.focus = 2;
    H.last_run = 0;

    for (k = 0; k < 4; k++) {
        int id = ma_pick();
        order[k] = id;
        if (id < APP_ID_MIN || id > APP_ID_MAX) {
            check(0, "14a 4 周とも 1 本選べる");
            return;
        }
        seen[id - APP_ID_MIN]++;
        ma_resume(id);
        ma_gui_call(MA_OP_WAIT);
        ma_park();
    }
    check(1, "14a 4 周とも 1 本選べる");
    check(order[0] == 2 && order[1] == 3 && order[2] == 4 && order[3] == 5,
          "14b 巡回の順は ID 昇順 (2,3,4,5)");
    check(seen[0] == 1 && seen[1] == 1 && seen[2] == 1 && seen[3] == 1,
          "14c 4 周で全員がちょうど 1 回ずつ走る (飢餓なし)");
    check(ma_pick() == 2, "14d 1 周したら先頭へ戻る");
}

/* ---- 15. 自作入力で park を回避できないこと (レビュー反例 1) ---- */
#define MA_STARVE_LIMIT 200
static void case_self_input_cannot_starve(void)
{
    int i, b_ran = 0, b_at = 0, parks = 0;
    ma_init(4096);
    ma_start(100, 1); ma_gui_call(MA_OP_WAIT); ma_park();
    ma_start(100, 1); ma_gui_call(MA_OP_WAIT); ma_park();
    ma_set_ready(3, 0, 1);
    H.focus = 2;
    ma_resume(2);

    for (i = 0; i < MA_STARVE_LIMIT; i++) {
        ma_gui_call(MA_OP_WAIT);
        ma_set_ready(2, 1, 0);
        if (ma_should_park()) {
            int k;
            ma_park();
            parks++;
            k = ma_pick();
            if (k <= 0) {
                check(0, "15a 自作入力を続けても park は必ず起きる");
                return;
            }
            ma_resume(k);
        } else {
            ma_gui_return();
        }
        if (appslot_cur() == 3 && !b_ran) { b_ran = 1; b_at = i + 1; }
    }
    check(parks > 0, "15a 自作入力を続けても park は必ず起きる");
    check(b_ran, "15b Paint 待ちの B が走る (飢餓しない)");
    check(b_ran && b_at == (2 * 2 - 2) * (MA_INPUT_STREAK_MAX + 1),
          "15c B は N=2 の上限 (10 回) にちょうど届く");
    check(b_ran && b_at <= MA_STARVE_BOUND,
          "15d B は定数の上限 (MA_STARVE_BOUND) の内側");
}

/* ---- 16. ラウンドをまたぐ待ちの上限 (レビュー反例 2) ---- */
static int run_cross_round(int focus_id)
{
    int i, n, k;
    ma_init(4096);
    for (i = 0; i < 4; i++) {
        ma_start(100, 1);
        ma_gui_call(MA_OP_WAIT);
        ma_park();
    }
    for (i = APP_ID_MIN; i <= APP_ID_MAX; i++) ma_set_ready(i, 1, 0);
    H.focus = APP_ID_MIN;
    k = ma_pick();
    if (k != APP_ID_MIN) return -1;
    ma_resume(k);
    ma_set_ready(APP_ID_MIN, 0, 1);
    H.focus = focus_id;
    for (n = 0; n < 200; n++) {
        ma_gui_call(MA_OP_WAIT);
        if (ma_should_park()) {
            ma_park();
            k = ma_pick();
            if (k <= 0) return -2;
            ma_resume(k);
            if (k == APP_ID_MIN) return n;
        } else {
            ma_gui_return();
        }
    }
    return 0;
}

static void case_cross_round_bound(void)
{
    int a2, a3, a5;
    a2 = run_cross_round(APP_ID_MIN);
    a3 = run_cross_round(APP_ID_MIN + 1);
    a5 = run_cross_round(APP_ID_MAX);
    check(a2 > 0, "16a 導出群の A は必ず走る (無限待ちにならない)");
    check(a2 <= MA_STARVE_BOUND, "16b A は再導出した上限以内に走る");
    check(a2 == MA_STARVE_BOUND,
          "16c 上限は緩くない (この構成でちょうど上限に届く)");
    check(a3 > 0 && a3 <= MA_STARVE_BOUND,
          "16d フォーカスが B でも上限を超えない");
    check(a5 > 0 && a5 <= MA_STARVE_BOUND,
          "16e フォーカスが D でも上限を超えない");
}

/* ---- 17. 印なし resume の拒否と、G7 の 4 カウンタ (票 K5b の追加) ----
 *  D0 の C5/C6: park は「OP_WAIT 由来」の印を立て、resume はその印のある
 *  フレームだけを起こす。印を人為的に落として負例を作る。 */
static void case_resume_needs_wait_mark(void)
{
    AppSlot *a;
    u32 bad0, sw0, rej0, tr0;

    ma_init(4096);
    fill_four(100);
    sw0 = ring3_switch_count;
    bad0 = ring3_resume_bad_frame_count;
    rej0 = ring3_park_reject_count;
    tr0 = ring3_transition_count;

    a = appslot_get(4);
    a->parked_from_wait = 0;              /* 印の無いフレームに見せる */
    check(ma_resume(4) == OS32_ERR_STALE,
          "17a 印の無いフレームは OS32_ERR_STALE で拒否される");
    check(ring3_resume_bad_frame_count == bad0 + 1,
          "17b 拒否のたび ring3_resume_bad_frame_count が増える");
    check(ring3_switch_count == sw0,
          "17c 拒否は ring3_switch_count を増やさない");
    check(appslot_get(4)->state == APP_STATE_PARKED && appslot_cur() == 1,
          "17d 拒否されたアプリは park のまま、WM は top-level のまま");

    a->parked_from_wait = 1;              /* 印を戻せば起きる */
    check(ma_resume(4) == 0, "17e 印のあるフレームは起こせる");
    check(ring3_switch_count == sw0 + 1 &&
          ring3_resume_bad_frame_count == bad0 + 1,
          "17f 成功は switch_count だけを増やす");
    check(appslot_get(4)->parked_from_wait == 0,
          "17g resume した時点で印は消える (二度は起こせない)");

    /* OP_WAIT 以外からの park は弾かれ、park_reject_count だけが増える */
    ma_gui_call(MA_OP_POLL);
    check(ma_park() == OS32_ERR_INVAL && ma_park() == OS32_ERR_INVAL,
          "17h OP_WAIT 以外の op からの park は 2 回とも弾かれる");
    check(ring3_park_reject_count == rej0 + 2,
          "17i 弾いた回数が ring3_park_reject_count に載る");
    ma_gui_return();
    check(ring3_transition_count == tr0,
          "17j park/resume は transition_count を動かさない (別勘定)");

    /* CUI の入れ子 exec_run の子は park できない: longjmp の行き先が
     * 「走っている親アプリの中の exec_run フレーム」になり WM へ戻れない。
     * 親は子の終了まで塞がっているので譲る相手も居ない (D4)。 */
    {
        /* ここまでで ID 4 が走っている (17e で resume した)。 */
        u32 rej1 = ring3_park_reject_count;
        check(ma_start(10, 0) == OS32_ERR_FULL,
              "17k GUI 4 本のときは CUI の子も立たない (池は 1 つ)");
        ma_exit(0);                       /* 走っている ID 4 を畳んで枠を空ける */
        check(appslot_cur() == APP_ID_SHELL && ma_start(10, 0) == 4,
              "17l 空いた枠に CUI の子 (gui=0) が立つ");
        ma_gui_call(MA_OP_WAIT);
        check(ma_park() == OS32_ERR_INVAL,
              "17m CUI の入れ子の子は OP_WAIT の中でも park できない");
        check(ring3_park_reject_count == rej1 + 1,
              "17n その拒否も park_reject_count に載る");
        ma_gui_return();
    }
}

/* ---- 18. シェル帯 (ID 1) は per-app 経路に巻き込まれない ----
 * 2026-09-11 の差し戻し (実機初回起動で「FATAL: shell.bin load failed」) で
 * 立てた境界。shell.bin も gshell.bin も exec ネスト段 0 = ID 1 で、
 * 0x300000 の MEM_SHELL_* 帯に identity で載る CPL=0 プログラム。
 * pgalloc の空きがどれだけ少なくても、per-app 物理・ID の池・枚数勘定の
 * どれにも掛からずに起動できなければならない (K5a 設計 D7「変えないもの」)。
 * 非シェルはローダが CPL=3 に限定する。 */
static void case_shell_never_takes_app_band(void)
{
    AppSlot *sh;

    /* (b) 物理が尽きていてもシェルは立つ。同じ空きでアプリは弾かれる。 */
    ma_init(1);          /* 空き 1 枚。0 は admit の「勘定しない」合図なので使わない */
    check(ma_start(64, 1) == EXEC_ERR_NOMEM,
          "18e 空き 1 枚では GUI アプリの起動は ERR_NOMEM");
    appslot_shell_commit();          /* exec_launch の is_shell 経路はこれだけ */
    sh = appslot_get(APP_ID_SHELL);
    check(sh != 0 && sh->state == APP_STATE_RUNNING &&
          appslot_cur() == APP_ID_SHELL,
          "18f 同じ空きでもシェルは起動する (枚数勘定を通らない)");
    check(sh->depth == 1 && sh->cpl3 == 0,
          "18g シェルは段 1 の CPL=0 (アプリ PD を持たない)");
    check(H.free_pages == 1,
          "18h シェルの起動は空きページを 1 枚も動かさない");
    check(appslot_live() == 0,
          "18i シェルは非シェル ID の池を消費しない");
    check(appslot_alloc_id() == APP_ID_MIN,
          "18j 池は手つかず — 次のアプリは ID 2 から");
    check(res_owner_get() == APP_ID_SHELL,
          "18k 資源の所有者はシェル帯 (owner 1) のまま");

    /* (c) gshell ⇔ CUI shell の載せ替え (sys_switch_shell / K4 の起動ループ)
     *     を繰り返しても、同じ ID 1 に留まり池も空きも動かない。 */
    appslot_shell_commit();
    appslot_shell_commit();
    check(appslot_cur() == APP_ID_SHELL && appslot_live() == 0 &&
          appslot_get(APP_ID_SHELL)->depth == 1,
          "18l 載せ替えを繰り返しても ID 1 / 段 1 のまま");
    check(H.free_pages == 1 && appslot_alloc_id() == APP_ID_MIN,
          "18m 載せ替えは空きも池も動かさない");
}



/* ---- 20. CTRL+STOP の要求を降ろす (KAPI v45 exec_abort_clear、決裁 A1) ----
 *
 * IRQ1 は宛先を選べず「いま走っているアプリ」に立てるが、契約 T6 の宛先は
 * フォーカス窓のアプリ。WM は本人の要求をこれで降ろしてからフォーカス窓の
 * ID を exec_kill で畳む。降ろせないと本人が次の安全地点で畳まれ、
 * 「意図しない 1 本が死ぬ」(票 K5b-W の A1 が書いた残る穴)。
 */
static void case_abort_clear(void)
{
    int owner0;
    int r;

    /* (a) owner 1 以外からは降ろせない (gui_register と同じ判定)。
     *     && で短絡させない (RED でも後ろが素通りしてしまう)。 */
    ma_init(4096);
    fill_four(100);
    ma_resume(3);
    ma_abort_request();
    check(appslot_get(3) != 0 && appslot_get(3)->abort_req == 1,
          "20a 下ごしらえ: 走っている 3 に CTRL+STOP が立つ");
    owner0 = res_owner_get();
    r = ma_abort_clear();
    check(owner0 == 3, "20b 下ごしらえ: owner は走っているアプリ");
    check(r == OS32_ERR_INVAL, "20c owner 1 以外からは OS32_ERR_INVAL");
    check(appslot_get(3) != 0 && appslot_get(3)->abort_req == 1,
          "20d 弾かれた呼び出しは要求を降ろさない");

    /* (b) park で WM top-level (owner 1) へ戻れば降ろせる。要求は park して
     *     も残るので、状態 (RUNNING/PARKED) では対象を絞らない。 */
    ma_gui_call(MA_OP_WAIT);
    r = ma_park();
    check(r == 0 && res_owner_get() == APP_ID_SHELL,
          "20e 下ごしらえ: park して WM top-level へ戻る");
    check(appslot_get(3) != 0 && appslot_get(3)->abort_req == 1,
          "20f 要求は park をまたいで残る");
    r = ma_abort_clear();
    check(r == 0, "20g owner 1 からは 0");
    check(appslot_get(3) != 0 && appslot_get(3)->abort_req == 0,
          "20h 要求が降りている");

    /* (c) 降ろすだけ — 他の ID にも、対象の ID の他の欄にも触らない。 */
    check(appslot_live() == 4 && appslot_reclaim_count == 0,
          "20i 降ろすだけで 1 本も畳まない");
    check(appslot_get(2) != 0 && appslot_get(2)->state == APP_STATE_PARKED &&
          appslot_get(2)->parked_from_wait == 1,
          "20j 別の ID は印ごと無傷");
    check(appslot_get(5) != 0 && appslot_get(5)->state == APP_STATE_PARKED &&
          appslot_get(5)->parked_from_wait == 1,
          "20k 別の ID は印ごと無傷 (末尾も)");
    check(appslot_get(3)->state == APP_STATE_PARKED &&
          appslot_get(3)->parked_from_wait == 1,
          "20l 対象の ID も abort_req 以外は動かない");

    /* (d) 降ろした後は次の安全地点で畳まれない (A1 の残る穴がふさがる)。 */
    r = ma_resume(3);
    check(r == 0, "20m 下ごしらえ: 3 を起こし直す");
    r = ma_abort_check();
    check(r == 0, "20n 降ろした後の安全地点は畳まない");
    check(appslot_live() == 4 && appslot_get(3) != 0 &&
          appslot_reclaim_count == 0,
          "20o 意図しない 1 本が死なない");

    /* (e) 要求が無ければ何も起きない (二度目も 0)。 */
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    r = ma_abort_clear();
    check(r == 0, "20p 要求が無くても 0");
    check(appslot_live() == 4 && appslot_reclaim_count == 0,
          "20q 要求が無いときは 1 本も動かさない");

    /* (f) 走っているアプリが 1 本も居ないときも 0 で、何も起きない。 */
    ma_init(4096);
    r = ma_abort_clear();
    check(r == 0, "20r アプリが 1 本も居なければ 0");
    check(appslot_live() == 0 && appslot_cur() == APP_ID_SHELL &&
          appslot_reclaim_count == 0,
          "20s 居なければ何も起きない");
}

/* ---- 19. GUI 中の kbd 待ち = 第 2 の park 点 (票 K7 D1 / D5 / §5 B) ----
 * K5b までの park 点は gui_call(OP_WAIT) の 1 つだけだった。K7 で
 * kbd_getchar / kbd_getkey が 2 つ目になり、状態 WAIT_KEY と印
 * parked_from_kbd が増える。ここで固定するのは 4 つ:
 *   (a) park すると WAIT_KEY + 印 + cur はシェル帯 (exec_app_state は 3)
 *   (b) 注入が空なら resume は OS32_ERR_AGAIN で、印も状態もそのまま
 *       (WM はその周を譲ってもう一度試せる)
 *   (c) 注入があれば resume が通り、**1 バイトだけ** EAX に入る
 *   (d) 印の取り違え (OP_WAIT の印で WAIT_KEY を起こす) は STALE、
 *       鍵待ちのアプリは exec_kill で畳める (D5) */
static void case_wait_key(void)
{
    int id;
    AppSlot *a;
    u32 sw0, bad0, rej0, park0;

    ma_init(4096);
    kbd_inject_discard();
    host_reader = 2;
    id = ma_start(100, 1);
    check(id == APP_ID_MIN, "19a GUI アプリが 1 本走る");
    sw0 = ring3_switch_count;
    bad0 = ring3_resume_bad_frame_count;
    rej0 = ring3_park_reject_count;
    park0 = ring3_kbd_park_count;

    /* (a) OP_WAIT の中でなくても park できる — 呼び手は kbd の syscall */
    check(ma_park_kbd() == 0, "19b OP_WAIT の外でも kbd 待ちなら park できる");
    check(ring3_kbd_park_count == park0 + 1,
          "19c ring3_kbd_park_count が増える");
    check(ring3_park_reject_count == rej0, "19d 正常な park は弾き数に載らない");
    a = appslot_get(id);
    check(a->state == APP_STATE_WAIT_KEY && a->parked_from_kbd == 1,
          "19e WAIT_KEY + kbd 由来の印");
    check(a->parked_from_wait == 0, "19f OP_WAIT の印は立たない");
    check(appslot_cur() == APP_ID_SHELL && res_owner_get() == APP_ID_SHELL,
          "19g cur と owner はシェル帯 (WM top-level) へ戻る");
    check(appslot_state(id) == APP_STATE_WAIT_KEY,
          "19h exec_app_state は 3 を返す");

    /* (b) 注入が空なら起こせない。印も状態も動かない */
    check(kbd_inject_pending() == 0, "19i 注入リングは空");
    check(ma_resume_kbd(id) == OS32_ERR_AGAIN,
          "19j 空の resume は OS32_ERR_AGAIN");
    check(ring3_switch_count == sw0, "19k 拒否は switch_count を増やさない");
    check(ring3_resume_bad_frame_count == bad0,
          "19l 空は「印なし」ではないので bad_frame_count も増えない");
    check(appslot_get(id)->state == APP_STATE_WAIT_KEY &&
          appslot_get(id)->parked_from_kbd == 1,
          "19m 印も状態もそのまま (次の周でもう一度試せる)");

    /* (c) 注入があれば 1 バイトだけ EAX に入る */
    res_owner_set(2);                      /* 端末アプリ (読み手) から注ぐ */
    check(kbd_inject((const u8 *)"ab", 2) == 2, "19n 端末アプリが 2 バイト注ぐ");
    res_owner_set(APP_ID_SHELL);           /* resume を呼ぶのは WM */
    check(ma_resume_kbd(id) == 0, "19o 注入があれば起こせる");
    check(appslot_get(id)->frame[APP_FRAME_EAX] == (u32)'a',
          "19p EAX には最初の 1 バイトだけが入る");
    check(kbd_inject_pending() == 1, "19q 残りは 1 バイト (まとめて渡さない)");
    check(ring3_switch_count == sw0 + 1, "19r 成功は switch_count を 1 増やす");
    check(appslot_get(id)->parked_from_kbd == 0,
          "19s 起こした時点で印は消える");

    /* (d) 印の取り違えと kill */
    check(ma_park_kbd() == 0, "19t もう一度 kbd 待ちで park できる");
    a = appslot_get(id);
    a->parked_from_kbd = 0;
    a->parked_from_wait = 1;               /* OP_WAIT の印だけに見せる */
    check(ma_resume_kbd(id) == OS32_ERR_STALE,
          "19u WAIT_KEY を OP_WAIT の印では起こせない");
    check(ring3_resume_bad_frame_count == bad0 + 1,
          "19v 印の取り違えは bad_frame_count に載る");
    check(appslot_kill_check(id) == 0,
          "19w 鍵待ちのアプリは exec_kill で畳める (D5)");
    a->parked_from_kbd = 1;
    check(ma_resume_kbd(id) == 0, "19x 印を戻せば起こせる (残りの 'b')");
    check(appslot_get(id)->frame[APP_FRAME_EAX] == (u32)'b',
          "19y 2 バイト目が次の resume で届く");
    check(kbd_inject_pending() == 0, "19z 注入リングは空に戻る");

    /* 走っている本人は kill できない / CUI の入れ子の子は park できない */
    check(appslot_kill_check(id) == OS32_ERR_INVAL,
          "19A 走っている間は exec_kill を呼べない (畳むのは CTRL+STOP)");
    {
        u32 rej1 = ring3_park_reject_count;
        int child = ma_start(10, 0);
        check(child > 0, "19B CUI の入れ子の子が立つ");
        check(ma_park_kbd() == OS32_ERR_INVAL,
              "19C CUI の入れ子の子は kbd 待ちでも park できない");
        check(ring3_park_reject_count == rej1 + 1,
              "19D その拒否は park_reject_count に載る");
        ma_exit(0);
    }
    ma_exit(0);
    check(appslot_cur() == APP_ID_SHELL, "19E 畳んだら WM top-level へ戻る");
    check(ma_park_kbd() == OS32_ERR_INVAL,
          "19F シェル帯 (WM top-level) からの park は弾かれる");
    kbd_inject_discard();
}


/* ======================================================================== */
/*  ケース 20 — 画面の所有者 (票 T8 D1 / D1a / D3、2026-09-12)               */
/*                                                                          */
/*  全画面 GFX プログラムが gshell 配下で走ると、画面の持ち主が 1 本に決まり  */
/*  ((a) 遷移)、宣言していないプログラムは画面を取れず ((b) 拒否)、VRAM を    */
/*  直接触る --cpl0 は GUI から起動できない ((c))。3 つとも実物の            */
/*  exec/appslot.c を叩く。                                                  */
/* ======================================================================== */
static void case_gfx_screen_owner(void)
{
    int id, other;
    u32 rej0;

    /* --- (a) 遷移: gfx_init で取り、回収で WM へ戻る ------------------- */
    ma_init(4096);
    check(appslot_gfx_owner() == GFX_OWNER_WM,
          "20a 起動直後の画面の所有者は WM (1)");

    id = ma_start_gfx(100, 1, OS32X_FLAG_GFX);
    check(id == APP_ID_MIN, "20b 下ごしらえ: 宣言付きの GFX アプリが 1 本立つ");
    check(appslot_gfx_owner() == GFX_OWNER_WM,
          "20c 起動しただけでは画面は WM のまま (gfx_init を呼んでいない)");

    check(appslot_gfx_claim(0) == 0 && appslot_gfx_owner() == GFX_OWNER_WM,
          "20d CUI 中 (con_sink 無効) は所有者を触らない");

    check(appslot_gfx_claim(1) == 0, "20e GUI 中の gfx_init は通る");
    check(appslot_gfx_owner() == id,
          "20f 画面の所有者は gfx_init を呼んだアプリへ移る");

    check(appslot_gfx_claim(1) == 0 && appslot_gfx_owner() == id,
          "20g 同じアプリが二度呼んでも所有者は変わらない");

    appslot_gfx_owner_exit(GFX_OWNER_WM);
    check(appslot_gfx_owner() == id,
          "20h 他人 (WM) の回収では所有者は戻らない");

    check(ma_exit(0) == 0, "20i アプリが終了する");
    check(appslot_gfx_owner() == GFX_OWNER_WM,
          "20j 所有者の回収で画面は WM へ戻る (D1)");

    /* kill (CTRL+STOP / exec_kill) でも同じ経路を通る。 */
    ma_init(4096);
    id = ma_start_gfx(100, 1, OS32X_FLAG_GFX);
    appslot_gfx_claim(1);
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    check(appslot_gfx_owner() == id,
          "20k park しただけでは画面はアプリのまま (WM は上書きしない)");
    check(ma_kill(id) == 0 && appslot_gfx_owner() == GFX_OWNER_WM,
          "20l exec_kill で畳んでも画面は WM へ戻る");

    /* 2 本目が取った画面は、1 本目の回収では戻らない。 */
    ma_init(4096);
    id = ma_start_gfx(100, 1, OS32X_FLAG_GFX);
    ma_gui_call(MA_OP_WAIT);
    ma_park();
    other = ma_start_gfx(100, 1, OS32X_FLAG_GFX);
    check(other == APP_ID_MIN + 1, "20m 下ごしらえ: 2 本目の GFX アプリ");
    check(appslot_gfx_claim(1) == 0 && appslot_gfx_owner() == other,
          "20n 2 本目が gfx_init を呼べば画面は 2 本目のもの");
    ma_exit(0);
    check(appslot_gfx_owner() == GFX_OWNER_WM, "20o 2 本目の回収で WM へ戻る");
    check(ma_resume(id) == 0, "20p 1 本目を起こす");
    ma_exit(0);
    check(appslot_gfx_owner() == GFX_OWNER_WM,
          "20q 画面を持っていない 1 本目の回収では所有者は動かない");

    /* --- (b) 宣言なしの拒否 (D1a) --------------------------------------- */
    ma_init(4096);
    rej0 = gfx_init_reject_count;
    id = ma_start_gfx(100, 1, 0);        /* app.conf に gfx 列が無いプログラム */
    check(id == APP_ID_MIN, "20r 下ごしらえ: 宣言の無いアプリが 1 本立つ");
    check(appslot_gfx_claim(1) == OS32_ERR_INVAL,
          "20s GUI 中に宣言の無い CPL=3 が gfx_init を呼べば ERR_INVAL");
    check(appslot_gfx_owner() == GFX_OWNER_WM,
          "20t 拒否で画面は WM のまま (gfx_init は呼ばれない)");
    check(gfx_init_reject_count == rej0 + 1,
          "20u 拒否は gfx_init_reject_count に載る");
    check(appslot_gfx_claim(0) == 0 && appslot_gfx_owner() == GFX_OWNER_WM,
          "20v CUI 中は宣言が無くても従来どおり通る");
    check(gfx_init_reject_count == rej0 + 1,
          "20w CUI 中の素通しは拒否として数えない");

    /* CPL=0 の子 (--cpl0) は宣言の有無に関わらず所有者を取らない。 */
    appslot_at(id)->cpl3 = 0;
    appslot_at(id)->hdr_flags = OS32X_FLAG_GFX;
    check(appslot_gfx_claim(1) == 0 && appslot_gfx_owner() == GFX_OWNER_WM,
          "20x CPL=0 の子は画面の所有者にならない");
    ma_exit(0);

    /* WM 自身 (シェル帯、owner 1) の復帰の gfx_init は素通し。 */
    check(appslot_cur() == APP_ID_SHELL, "20y WM top-level へ戻っている");
    check(appslot_gfx_claim(1) == 0 && appslot_gfx_owner() == GFX_OWNER_WM,
          "20z WM の復帰の gfx_init は所有者を動かさない");


}


/* ======================================================================== */
/*  ケース 21 — CUI 専用の宣言と「拒否は畳む」 (票 T8-2、2026-09-12)         */
/*                                                                          */
/*  受入 F5 の不合格から。T8 の砦は 0x0004 だけだったが、      */
/*  `userland/cmds/v86.bin` の flags は 0x0 — v86 は CPL=3 のプログラムで、   */
/*  V86 へは KAPI (v86_*) を通してカーネル側から入る。宣言ビット              */
/*  OS32X_FLAG_CUI_ONLY を 1 つ足して GUI からの起動そのものを断つ。          */
/*                                                                          */
/*  受入 F6 の実測から。宣言の無い gfx_init を「断って続行させる」と、        */
/*  プログラムは失敗を知らないまま描画 KAPI と VRAM 直書きで描き続け GUI を   */
/*  壊した。拒否 = そのアプリを畳む (abort_req → syscall 出口)。             */
/* ======================================================================== */
static void case_cui_only_and_reject_kill(void)
{
    int id;
    u32 rej0;

    /* --- (a) 純関数: GUI からだけ断る ---------------------------------- */
    check(appslot_cui_only_admit(1, OS32X_FLAG_CUI_ONLY) == OS32_ERR_INVAL,
          "21a GUI からの CUI 専用プログラムは ERR_INVAL");
    check(appslot_cui_only_admit(0, OS32X_FLAG_CUI_ONLY) == 0,
          "21b CUI (exec_run) からは従来どおり通る");
    check(appslot_cui_only_admit(1, 0) == 0,
          "21c 宣言の無いプログラムは GUI からでも通る");
    check(appslot_cui_only_admit(1, (u32)(OS32X_FLAG_CUI_ONLY |
                                          OS32X_FLAG_GFX)) == OS32_ERR_INVAL,
          "21d 他のビットと混ざっていても宣言を見る");
    check(appslot_cui_only_admit(1, 0x0004) == 0,
          "21e FORCE_CPL0 は別の砦 (v86.bin は flags 0x0 で素通りしていた = F5)");

    /* --- (b) exec_start 経路 (GUI) は起動しない、CUI は通る ------------- */
    ma_init(4096);
    check(ma_start_cui_only(1) == OS32_ERR_INVAL,
          "21f exec_start 経路の CUI 専用プログラムは起動しない");
    check(appslot_live() == 0 && H.free_pages == 4096,
          "21g 拒否は池も枚数も 1 つも動かさない");
    check(appslot_alloc_id() == APP_ID_MIN, "21h 拒否は池を消費しない");
    check(ma_start_cui_only(0) == APP_ID_MIN,
          "21i CUI (exec_run) 経路は従来どおり通る");
    check(appslot_live() == 1, "21j 通れば 1 本立つ");
    ma_exit(0);

    /* --- (c) gfx_init の拒否はアプリを畳む (受入 F6 の実測) ------------- */
    ma_init(4096);
    rej0 = gfx_init_reject_count;
    id = ma_start_gfx(100, 1, 0);        /* 宣言の無い CPL=3 */
    check(id == APP_ID_MIN, "21k 下ごしらえ: 宣言の無いアプリが 1 本立つ");
    check(appslot_at(id)->abort_req == 0, "21l 起動直後は畳む要求は無い");
    check(appslot_gfx_claim(1) == OS32_ERR_INVAL,
          "21m GUI 中の宣言なしの gfx_init は拒否される");
    check(appslot_at(id)->abort_req == 1,
          "21n 拒否したアプリは abort_req を負う (syscall 出口で畳まれる)");
    check(gfx_init_reject_count == rej0 + 1,
          "21o 数えるのは gfx_init_reject_count のまま (専用カウンタは増やさない)");
    appslot_at(id)->abort_req = 0;
    check(appslot_gfx_claim(0) == 0 && appslot_at(id)->abort_req == 0,
          "21p CUI 中の素通しは畳まない");
    check(appslot_gfx_claim(1) == OS32_ERR_INVAL &&
          appslot_at(id)->abort_req == 1,
          "21q 二度目の拒否でも畳む要求は立つ");
    ma_exit(0);
    check(appslot_live() == 0, "21r 畳んだ後は 1 本も残らない");
}

/* ======================================================================== */
/*  ケース 22 — ポーリング型の協調 yield (票 T8 §7 D8、2026-09-12)           */
/*                                                                          */
/*  GUI 中に kbd_trygetchar が回っているだけのプログラム (gfx200_test の FPS  */
/*  段) は、K7 の park 点 (WAIT_KEY = キーが来るまで起こさない) では譲れない  */
/*  — 「無ければ -1」で戻る約束を破ってしまう。そこで **1 周だけ** 譲る第 3   */
/*  の park 点を足した。ここで固定するのは 5 つ:                              */
/*    (a) park すると WAIT_POLL + 印 + cur はシェル帯 (exec_app_state は 4)   */
/*    (b) 注入が空でも resume は通り、EAX に **-1** が入る (WAIT_KEY との差)  */
/*    (c) 注入があれば 1 バイトだけ EAX に入る                                */
/*    (d) **前回の譲りから tick が進んでいなければ park しない** (10ms に     */
/*        1 回まで)。弾き数 ring3_park_reject_count には載らない              */
/*    (e) 譲り中のアプリは exec_kill で畳める / 印なしの resume は STALE      */
/* ======================================================================== */
static void case_poll_yield(void)
{
    int id;
    AppSlot *a;
    u32 sw0, bad0, rej0, yield0, kbdpark0;
    u32 t = 1000;

    ma_init(4096);
    kbd_inject_discard();
    appslot_poll_yield_reset();
    host_reader = 2;
    id = ma_start(100, 1);
    check(id == APP_ID_MIN, "22a GUI アプリが 1 本走る");
    sw0 = ring3_switch_count;
    bad0 = ring3_resume_bad_frame_count;
    rej0 = ring3_park_reject_count;
    yield0 = ring3_poll_yield_count;
    kbdpark0 = ring3_kbd_park_count;

    /* (a) OP_WAIT の外でも、tick が進んでいれば 1 周だけ譲れる */
    check(ma_park_poll(t) == 0, "22b tick が進んでいれば譲れる");
    check(ring3_poll_yield_count == yield0 + 1,
          "22c ring3_poll_yield_count が増える");
    check(ring3_park_reject_count == rej0, "22d 正常な譲りは弾き数に載らない");
    check(ring3_kbd_park_count == kbdpark0, "22e WAIT_KEY の勘定とは別");
    a = appslot_get(id);
    check(a->state == APP_STATE_WAIT_POLL && a->parked_from_poll == 1,
          "22f WAIT_POLL + ポーリング由来の印");
    check(a->parked_from_wait == 0 && a->parked_from_kbd == 0,
          "22g 他の 2 つの印は立たない");
    check(appslot_cur() == APP_ID_SHELL && res_owner_get() == APP_ID_SHELL,
          "22h cur と owner はシェル帯 (WM top-level) へ戻る");
    check(appslot_state(id) == APP_STATE_WAIT_POLL,
          "22i exec_app_state は 4 を返す");

    /* (b) 注入が空でも起こす。EAX は -1 (キーなし) */
    check(kbd_inject_pending() == 0, "22j 注入リングは空");
    check(ma_resume_poll(id) == 0, "22k 空でも resume は通る (WAIT_KEY との差)");
    check(appslot_get(id)->frame[APP_FRAME_EAX] == (u32)(i32)-1,
          "22l EAX には -1 (キーなし) が入る");
    check(ring3_switch_count == sw0 + 1, "22m 成功は switch_count を 1 増やす");
    check(appslot_get(id)->parked_from_poll == 0,
          "22n 起こした時点で印は消える");
    check(appslot_get(id)->state == APP_STATE_RUNNING, "22o 走っている状態へ戻る");

    /* (d) 同じ tick では 2 度譲らない。弾き数にも載らない */
    rej0 = ring3_park_reject_count;
    yield0 = ring3_poll_yield_count;
    check(ma_park_poll(t) == OS32_ERR_AGAIN,
          "22p 同じ tick では譲らない (10ms に 1 回まで)");
    check(ring3_poll_yield_count == yield0, "22q 譲っていないので数えない");
    check(ring3_park_reject_count == rej0,
          "22r 間引きは違反ではないので弾き数に載らない");
    check(appslot_get(id)->state == APP_STATE_RUNNING &&
          appslot_cur() == id, "22s 走ったまま (park していない)");

    /* (c) tick が進み、注入があれば 1 バイトだけ EAX に入る */
    t++;
    check(ma_park_poll(t) == 0, "22t tick が 1 つ進めばまた譲れる");
    res_owner_set(2);                      /* 端末アプリ (読み手) から注ぐ */
    check(kbd_inject((const u8 *)"ab", 2) == 2, "22u 端末アプリが 2 バイト注ぐ");
    res_owner_set(APP_ID_SHELL);           /* resume を呼ぶのは WM */
    check(ma_resume_poll(id) == 0, "22v 注入があっても resume は通る");
    check(appslot_get(id)->frame[APP_FRAME_EAX] == (u32)'a',
          "22w EAX には最初の 1 バイトだけが入る");
    check(kbd_inject_pending() == 1, "22x 残りは 1 バイト (まとめて渡さない)");

    /* (e) 印なしの resume は STALE / 譲り中でも畳める */
    t++;
    check(ma_park_poll(t) == 0, "22y もう一度譲れる");
    a = appslot_get(id);
    a->parked_from_poll = 0;
    a->parked_from_kbd = 1;                /* kbd の印だけに見せる */
    check(ma_resume_poll(id) == OS32_ERR_STALE,
          "22z WAIT_POLL を kbd の印では起こせない");
    check(ring3_resume_bad_frame_count == bad0 + 1,
          "22A 印の取り違えは bad_frame_count に載る");
    check(appslot_kill_check(id) == 0,
          "22B 譲り中のアプリは exec_kill で畳める");
    a->parked_from_kbd = 0;
    a->parked_from_poll = 1;
    check(ma_resume_poll(id) == 0, "22C 印を戻せば起こせる (残りの 'b')");
    check(appslot_get(id)->frame[APP_FRAME_EAX] == (u32)'b',
          "22D 2 バイト目が次の resume で届く");
    check(kbd_inject_pending() == 0, "22E 注入リングは空に戻る");

    /* 走っている本人は kill できない / CUI の入れ子の子は譲れない */
    check(appslot_kill_check(id) == OS32_ERR_INVAL,
          "22F 走っている間は exec_kill を呼べない");
    {
        u32 rej1;
        int child;
        t++;
        child = ma_start(10, 0);
        check(child > 0, "22G CUI の入れ子の子が立つ");
        rej1 = ring3_park_reject_count;
        check(ma_park_poll(t) == OS32_ERR_INVAL,
              "22H CUI の入れ子の子はポーリングでも譲れない");
        check(ring3_park_reject_count == rej1 + 1,
              "22I その拒否は park_reject_count に載る");
        ma_exit(0);
    }
    ma_exit(0);
    t++;
    check(appslot_cur() == APP_ID_SHELL, "22J 畳んだら WM top-level へ戻る");
    rej0 = ring3_park_reject_count;
    check(ma_park_poll(t) == OS32_ERR_INVAL,
          "22K シェル帯 (WM top-level) からの譲りは弾かれる");
    check(ring3_park_reject_count == rej0 + 1, "22L その拒否は弾き数に載る");

    /* (f) 間引きは表の検査より**先**で、控えは弾かれた試みでも進む。
     * ポーリングは秒間数万回来るので、譲れない文脈の連打で
     * ring3_park_reject_count が跳ね上がってはいけない。 */
    check(ma_park_poll(t) == OS32_ERR_AGAIN,
          "22M 同じ tick の連打は間引きで止まる (間引きは表の検査より先)");
    check(ring3_park_reject_count == rej0 + 1,
          "22N 弾き数も tick ごと 1 回まで");

    /* 控えの巻き戻し: reset で「次の 1 回」が必ず通る */
    appslot_poll_yield_reset();
    kbd_inject_discard();
}

/* ========================================================================
 *  23. 明示的な譲り sys_yield (票 T9 D5) と exec_kill の連鎖 (票 T9 D8)
 *
 *  ここで見るのは「表 (launch) と AppSlot が噛み合うところ」だけ:
 *    - sys_yield の park は WAIT_POLL のまま印だけが違い、resume は
 *      **注入リングを読まない** (sh が子宛の打鍵を吸って捨てない)。
 *    - exec_kill は要求表の連鎖を末尾から畳み、各段の回収通知が親の表を
 *      DONE + child = 0 にする。CTRL+STOP は末尾 1 本だけ。
 *  表そのものの遷移は tools/tests/launch_host.c の担当。
 * ======================================================================== */
static void case_yield_and_kill_chain(void)
{
    int term, sh, child;
    AppSlot *a;
    u32 yield0, poll0;
    i32 t_term, t_sh;
    char buf[LAUNCH_CMDLINE_MAX];

    ma_init(4096);
    kbd_inject_discard();
    appslot_poll_yield_reset();
    launch_init();
    host_gui = 1;

    term = ma_start_gfx(100, 1, (u32)OS32X_FLAG_LAUNCHER);
    check(term == APP_ID_MIN, "23a 端末 (宣言 LAUNCHER) が立つ");
    host_reader = term;                      /* con_sink の読み手 = 端末 */
    t_term = launch_req("sh");
    check(t_term > 0, "23b 端末が sh の起動を要求できる");

    yield0 = ring3_yield_count;
    poll0 = ring3_poll_yield_count;
    check(ma_park_yield() == 0, "23c sys_yield は tick の間引き無しで譲れる");
    check(ring3_yield_count == yield0 + 1, "23d ring3_yield_count が増える");
    check(ring3_poll_yield_count == poll0, "23e ポーリングの勘定とは別");
    a = appslot_get(term);
    check(a->state == APP_STATE_WAIT_POLL, "23f 状態は WAIT_POLL のまま");
    check(a->parked_from_yield == 1 && a->parked_from_poll == 0,
          "23g 印は parked_from_yield だけ");
    check(a->parked_from_wait == 0 && a->parked_from_kbd == 0,
          "23h 他の印は立たない");
    check(appslot_state(term) == APP_STATE_WAIT_POLL,
          "23i exec_app_state は 4 のまま (値を増やさない)");
    check(appslot_cur() == APP_ID_SHELL && res_owner_get() == APP_ID_SHELL,
          "23j WM top-level へ戻る");

    /* 譲っている間に届いた打鍵は子のもの。resume で吸ってはいけない
     * (票 §6 blocker 1 = parked_from_yield を足した理由そのもの)。 */
    res_owner_set(term);
    check(kbd_inject((const u8 *)"ab", 2) == 2, "23k 読み手が 2 バイト注ぐ");
    res_owner_set(APP_ID_SHELL);
    check(kbd_inject_pending() == 2, "23l 注入リングに 2 バイト");
    check(ma_resume_poll(term) == 0, "23m 譲りからの resume は通る");
    check(appslot_get(term)->frame[APP_FRAME_EAX] == 0,
          "23n EAX は 0 (sys_yield が普通に戻ったように見える)");
    check(kbd_inject_pending() == 2,
          "23o 注入リングは不変 (子宛の打鍵を吸わない)");
    check(appslot_get(term)->parked_from_yield == 0, "23p 印は 1 回きり");
    kbd_inject_discard();

    /* WM が要求を取り、sh を起動して結果を返す (端末 -> sh -> 子) */
    check(ma_park_yield() == 0, "23q 端末はもう一度譲る");
    check(launch_pending() == 1, "23r WM から見て要求が 1 本");
    check(launch_take(buf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0) == t_term,
          "23s WM が top-level で取る");
    sh = ma_start_gfx(100, 1, (u32)OS32X_FLAG_LAUNCHER);
    check(sh > 0, "23t sh が立つ");
    t_sh = launch_req("kbd_echo");
    check(t_sh > t_term, "23u sh も自分の表から要求できる");
    check(ma_park_yield() == 0, "23v sh が譲る");
    check(launch_report(t_term, sh) == 0, "23w WM が子 ID を表へ返す");
    check(launch_take(buf, (u32)LAUNCH_CMDLINE_MAX, 0, 0, 0) == t_sh,
          "23x 次は sh の要求");
    child = ma_start_gfx(100, 1, 0);
    check(child > 0, "23y 子が立つ");
    check(ma_park_yield() == 0, "23z 子が譲る");
    check(launch_report(t_sh, child) == 0, "23A 子の ID も表へ返る");

    check(launch_child((i32)term) == (i32)sh, "23B 端末の子は sh");
    check(launch_child((i32)sh) == (i32)child, "23C sh の子は 子");
    check(launch_child((i32)child) == 0, "23D 子が連鎖の末尾");

    /* CTRL+STOP: WM は launch_child で末尾を解決し、その 1 本だけ畳む */
    check(ma_kill_chain(child) == 0, "23E 末尾を畳む");
    check(ma_kill_order_n == 1 && ma_kill_order[0] == child,
          "23F 末尾は子孫を持たないので 1 本だけ");
    check(appslot_state(child) == 0, "23G 子は FREE");
    check(launch_child((i32)sh) == 0, "23H 回収通知で sh の表は child = 0");
    check(launch_child((i32)term) == (i32)sh, "23I 端末の表は sh のまま");

    /* exec_kill(id) は id と子孫を **末尾から** 畳む (票 D8) */
    check(ma_kill_chain(term) == 0, "23J 端末を畳むと子孫ごと");
    check(ma_kill_order_n == 2, "23K 連鎖の 2 本を畳んだ");
    check(ma_kill_order[0] == sh && ma_kill_order[1] == term,
          "23L 末尾 (sh) から順に畳む");
    check(appslot_state(sh) == 0 && appslot_state(term) == 0,
          "23M どちらも FREE");
    check(launch_pending() == 0, "23N 孤児回収は残らない");
    check(launch_child((i32)term) == 0 && launch_child((i32)sh) == 0,
          "23O 表は全部 IDLE (ERR_FULL で固着しない)");

    /* 譲れない文脈: CUI の入れ子の子と WM top-level */
    {
        u32 rej0 = ring3_park_reject_count;
        check(ma_park_yield() == OS32_ERR_INVAL,
              "23P シェル帯 (WM top-level) からは譲れない");
        check(ring3_park_reject_count == rej0 + 1, "23Q その拒否は弾き数に載る");
        term = ma_start_gfx(10, 0, (u32)OS32X_FLAG_LAUNCHER);  /* 入れ子の子 */
        check(term > 0, "23R CUI の入れ子の子が立つ");
        check(launch_req("ls") == OS32_ERR_INVAL,
              "23S 入れ子の子からの launch_req は断る");
        check(ma_park_yield() == OS32_ERR_INVAL,
              "23T 入れ子の子は sys_yield でも譲れない");
        check(ring3_park_reject_count == rej0 + 2, "23U その拒否も弾き数に載る");
        ma_exit(0);
    }
    host_reader = 2;
    kbd_inject_discard();
    launch_init();
}

/* ========================================================================
 *  24. 標準 FD のリダイレクト表は ID の文脈 (票 T9 §12 T1)
 *
 *  表 (fs/fd_redirect.c) は FD 0/1/2 の 3 本しかなく全アプリ共有だった。
 *  park してある sh のリダイレクトが生きたままなので:
 *    反例 1: WM の Start → Run で起動した別アプリの printf が sh の
 *            `> /tmp/out` に入る
 *    反例 2: パイプ中は stdout が sh の .bss (sh の**仮想**番地) なので、
 *            別アプリの sys_write(1) がその番地を別 CR3 で解決して書く
 *  park で走っていた ID の枠へ移し、resume で戻す。
 * ======================================================================== */
static void host_reset_files(void)
{
    int i;
    for (i = 0; i < HOST_VFS_MAX_FD; i++) {
        host_fd_open[i] = 0;
        host_fd_owner[i] = 0;
        host_fd_written[i] = 0;
        host_fd_close_calls[i] = 0;
    }
    host_fd_closes = 0;
    host_next_fd = 0;
}

static void case_redirect_context(void)
{
    static u8 pipebuf[16];
    int a2, a3;
    int closes0;
    u32 i;

    report("24 リダイレクト表は ID の文脈 (park/resume で持ち替える)\n");

    /* --- 反例 1: 別アプリの stdout が sh のファイルへ入らない ---------- */
    ma_init(4096);
    host_reset_files();
    fd_redirect_init();

    a2 = ma_start_gfx(100, 1, 0);                  /* sh 相当 */
    check(a2 == APP_ID_MIN, "24a sh が立つ");
    check(fd_redirect_to_file(1, "/tmp/out", FD_REDIR_WRITE) == 0,
          "24b sh が stdout をファイルへ張る");
    check(fd_redirect_write(1, "hello", 5) == 5, "24c sh の printf がファイルへ");
    check(host_fd_written[0] == 5, "24d ファイルに 5 バイト");

    check(ma_park_yield() == 0, "24e sh が park する (ask の WAIT_KEY 相当)");
    check(fd_is_redirected(1) == 0,
          "24f park でいまの表はコンソールへ戻る (枠へ移した)");

    a3 = ma_start_gfx(100, 1, 0);                  /* WM の Start -> Run */
    check(a3 > 0 && a3 != a2, "24g WM が別アプリを起動する");
    check(fd_redirect_write(1, "XXXX", 4) == -1,
          "24h 別アプリの stdout はコンソール (リダイレクトされていない)");
    check(host_fd_written[0] == 5,
          "24i 別アプリの printf は sh の /tmp/out に入らない (反例 1)");

    check(ma_park_yield() == 0, "24j 別アプリも譲る");
    check(ma_resume_poll(a2) == 0, "24k sh を起こす");
    check(fd_is_redirected(1) == 1, "24l resume で sh の表が戻る");
    check(fd_redirect_write(1, "!", 1) == 1 && host_fd_written[0] == 6,
          "24m 続きは同じファイルへ入る");

    /* --- 反例 2: パイプ中のバッファ (sh の .bss) を他人が書かない ------ */
    for (i = 0; i < 16; i++) pipebuf[i] = 0;
    check(fd_redirect_to_buffer(1, pipebuf, 16u, 0) == 0,
          "24n sh が stdout をパイプバッファへ張る");
    check(fd_redirect_write(1, "ab", 2) == 2, "24o sh がバッファへ 2 バイト");
    check(fd_redirect_get_buf_len(1) == 2, "24p sys_redirect_get_buf_len が 2");

    /* d0b: exercise the real appslot park/resume wiring with a value identity. */
    redir_table[1].access.origin = REDIR_USER;
    redir_table[1].access.app_id = a2;
    redir_table[1].access.as = (struct addrspace *)pipebuf;
    redir_table[1].access.pd_phys = 0x400000;
    redir_table[1].access.owner = 11;
    redir_table[1].access.generation = 123;
    check(ma_park_yield() == 0, "24q sh が park する");
    check(fd_is_redirected(1) == 0, "24r バッファも枠へ移る");
    check(ma_resume_poll(a3) == 0, "24s 別アプリを起こす");
    check(fd_redirect_write(1, "ZZZZ", 4) == -1,
          "24t 別アプリの stdout は sh のバッファを指さない");
    check(pipebuf[2] == 0,
          "24u sh の .bss は書かれない (別 CR3 の番地を書かない。反例 2)");
    check(ma_park_yield() == 0, "24v 別アプリが譲る");
    check(ma_resume_poll(a2) == 0, "24w sh を起こす");
    check(fd_redirect_get_buf_len(1) == 2, "24x sh のバッファ長は 2 のまま");
    check(redir_table[1].access.origin == REDIR_USER &&
          redir_table[1].access.app_id == a2 &&
          redir_table[1].access.as == (struct addrspace *)pipebuf &&
          redir_table[1].access.pd_phys == 0x400000 &&
          redir_table[1].access.owner == 11 &&
          redir_table[1].access.generation == 123,
          "24x2 park/resume preserves the entire registered identity");

    /* --- 回収: park したまま畳まれた ID は枠の中を閉じる ---------------- */
    ma_init(4096);
    host_reset_files();
    fd_redirect_init();
    a2 = ma_start_gfx(100, 1, 0);
    check(fd_redirect_to_file(1, "/tmp/out", FD_REDIR_WRITE) == 0,
          "24y 張ってから park する");
    check(ma_park_yield() == 0, "24z park");
    closes0 = host_fd_closes;
    check(host_fd_owner[0] == a2, "24A0 file_fd はその ID の owner タグを持つ");
    check(ma_kill(a2) == 0, "24A park 中のアプリを畳む");
    check(host_fd_closes == closes0 + 1,
          "24B 枠の中のファイルが閉じられる (vfs_close_owned が閉じる)");
    check(host_fd_close_calls[0] == 1,
          "24B2 vfs_close は 1 回しか呼ばれない (枠からは閉じない。往復 9)");
    check(host_fd_open[0] == 0, "24C ファイルは開いたままにならない");
    check(fd_is_redirected(1) == 0, "24D WM の表は触らない");
    /* 枠は空に戻す。閉じるのは vfs_close_owned なので close 回数では
     * 見えない — 「再利用 ID へ古い表を渡さない」を直に見る。 */
    check(fd_redirect_state_active(&g_redir[a2], 1) == 0,
          "24D2 畳んだ ID の枠は空に戻る (再利用 ID へ古い表を渡さない)");

    /* --- 走ったまま終わった ID は二重 close にならない ------------------ */
    ma_init(4096);
    host_reset_files();
    fd_redirect_init();
    a2 = ma_start_gfx(100, 1, 0);
    check(fd_redirect_to_file(1, "/tmp/o2", FD_REDIR_WRITE) == 0, "24E 張る");
    check(ma_park_yield() == 0, "24F 一度譲る (枠へ移る)");
    check(ma_resume_poll(a2) == 0, "24G 起こす (枠から戻る)");
    closes0 = host_fd_closes;
    check(ma_exit(0) == 0, "24H 走ったまま終わる");
    check(host_fd_closes == closes0 + 1,
          "24I 閉じるのは 1 回だけ (いまの表から外すときに閉じる)");
    check(host_fd_close_calls[0] == 1,
          "24J2 こちらも vfs_close は 1 回だけ (枠は resume で空)");
    check(fd_is_redirected(1) == 0, "24J 表はコンソールへ戻る");
}

/* ========================================================================
 *  25. GUI 中の CTRL+STOP は WM が宛先を決める (票 T9 §12 S6)
 *
 *  実機の反例 (feat/gui dc2f78d): 端末 (2) -> sh (3) -> kbd_echo (4) の連鎖で
 *  端末にフォーカスを置いて CTRL+STOP を 2 回打つと、2 回目で **sh と端末が
 *  両方消えた** (`ring3_abort_count` +1)。T9 で sh が WAIT_POLL で毎 tick
 *  回り端末も 100ms タイマで回るので、IRQ1 が落ちた先は宛先 (連鎖の末尾、
 *  D8) と無関係なアプリになる。GUI 中はカーネルが立てない。
 * ======================================================================== */
static void case_abort_admit(void)
{
    int a2, a3;

    report("25 GUI 中の CTRL+STOP はカーネルが宛先を決めない\n");

    /* 端末 (2) と sh (3) を立てて、sh が走っている状態にする */
    ma_init(4096);
    a2 = ma_start(100, 1);
    check(a2 == APP_ID_MIN, "25a 端末が立つ");
    appslot_mark_scheduled(a2, 1000);
    check(ma_park_yield() == 0, "25b 端末が譲る");
    a3 = ma_start(100, 1);
    check(a3 == APP_ID_MIN + 1, "25c sh が立つ");
    appslot_mark_scheduled(a3, 1000);

    /* (a) GUI 中: 走っている sh に IRQ1 の要求は載らない */
    check(ma_irq_abort_request(1, 1000) == 0,
          "25d GUI 中の IRQ1 は要求を立てない");
    check(appslot_get(a3)->abort_req == 0, "25e sh に abort_req が立たない");
    check(ma_abort_check() == 0 && appslot_get(a3) != 0,
          "25f syscall 出口でも畳まれない");
    check(appslot_live() == 2, "25g 生存アプリは減らない (端末も無事)");

    /* (b) GUI 中でも暴走 (2 秒 WM へ戻らない) は畳む */
    check(ma_irq_abort_request(1, 1000 + APP_RUNAWAY_TICKS - 1) == 0,
          "25h 境界の 1 つ手前ではまだ立てない");
    check(ma_irq_abort_request(1, 1000 + APP_RUNAWAY_TICKS) == 1,
          "25i APP_RUNAWAY_TICKS 以上 WM へ戻っていなければ立てる");
    check(appslot_get(a3)->abort_req == 1, "25j 暴走したアプリに載る");
    check(ma_abort_check() == 0, "25k 次の安全地点で畳まれる");
    check(appslot_get(a3) == 0 && appslot_get(a2) != 0,
          "25l 畳まれるのは暴走した 1 本だけ (端末は無事)");
    check(appslot_cur() == APP_ID_SHELL, "25m 畳んだあと WM top-level へ戻る");

    /* (c) resume で起点が更新される = 譲っている限り暴走にならない */
    check(ma_resume_poll(a2) == 0, "25n 端末を起こす");
    appslot_mark_scheduled(a2, 5000);          /* exec_resume の直後 */
    check(ma_irq_abort_request(1, 5000 + APP_RUNAWAY_TICKS - 1) == 0,
          "25o 起点が進むので暴走にならない");
    check(appslot_get(a2)->abort_req == 0, "25p 端末に abort_req は立たない");

    /* (d) gfx 拒否 / V86 の直呼びは GUI 中でも立つ (関門を通らない) */
    check(ma_abort_request() == 0, "25q 直呼び (gfx 拒否 / V86) は GUI 中も立つ");
    check(appslot_get(a2)->abort_req == 1, "25r その要求は載る");
    appslot_get(a2)->abort_req = 0;

    /* (e) WM (シェル帯) が走っているときは立てない */
    check(ma_park_yield() == 0, "25s 端末が譲る (cur = シェル帯)");
    check(ma_irq_abort_request(1, 99999) == 0,
          "25t WM top-level への IRQ1 は誰にも載せない");
    check(ma_irq_abort_request(0, 99999) == 0, "25u CUI でも同じ");

    /* --- CUI 中は 1 バイトも変えない (K2 の逃げ道) --------------------- */
    ma_init(4096);
    a2 = ma_start(100, 0);                     /* CUI の入れ子 exec_run の子 */
    check(a2 == APP_ID_MIN, "25v CUI で子が立つ");
    appslot_mark_scheduled(a2, 1000);
    check(ma_irq_abort_request(0, 1000) == 1,
          "25w CUI 中は tick を問わず従来どおり立てる");
    check(appslot_get(a2)->abort_req == 1, "25x 走っている子に載る");
    check(ma_abort_check() == 0 && appslot_get(a2) == 0,
          "25y 次の安全地点で畳まれる (K2 の逃げ道は健在)");

    /* --- (f) 待っているアプリは暴走ではない (票 §12 S6b) ---------------
     * GetMessage 型の GUI アプリ (端末) は、WM がその op_wait の**中**で
     * 回っている間 resume を通らない。start / resume だけを起点にすると、
     * 2 秒イベントを待っただけで「暴走」に見え、IRQ1 が CPL=3 の端末に
     * 落ちたときに **D8 の宛先より先に端末が畳まれる**。起点を
     * 「最後にカーネルへ入った tick」にすれば、KAPI を呼んでいる限り
     * 対象外になる。 */
    ma_init(4096);
    a2 = ma_start(100, 1);
    check(a2 == APP_ID_MIN, "25z 端末が立つ");
    appslot_mark_scheduled(a2, 1000);
    {
        u32 t;
        /* 300 tick = 3 秒ぶん、10 tick ごとに KAPI を 1 本呼ぶ
         * (描画 / キー取り / タイマ — op_wait の中で待っている形)。 */
        for (t = 1000; t <= 1300; t += 10) ma_syscall_enter(t);
    }
    check(ma_irq_abort_request(1, 1300) == 0,
          "25A 300 tick 待っても、その間 KAPI を呼んでいれば暴走ではない");
    check(appslot_get(a2) != 0 && appslot_get(a2)->abort_req == 0,
          "25B 端末に abort_req は立たない (D8 の宛先より先に畳まれない)");
    check(ma_irq_abort_request(1, 1300 + APP_RUNAWAY_TICKS - 1) == 0,
          "25C 最後の KAPI から境界の 1 つ手前ではまだ立てない");
    check(ma_irq_abort_request(1, 1300 + APP_RUNAWAY_TICKS) == 1,
          "25D 最後の KAPI から 200 tick 以上なら暴走として立てる");
    check(appslot_get(a2)->abort_req == 1, "25E 計算ループには載る");
    check(ma_abort_check() == 0 && appslot_get(a2) == 0,
          "25F 畳まれる (KAPI を呼ばない計算ループの唯一の逃げ道)");

    /* --- (g) K5c の経路は残す (票 §12 S6c) -----------------------------
     * gui_call(OP_WAIT) の中 = WM がそのアプリの syscall の中で回っている。
     * 割り込まれた文脈は CPL=0 (WM のコード) なので IRQ1 スタブの即 kill は
     * 起きず、要求は必ず WM のハンドラが見る (本人宛なら break して syscall
     * 出口で畳み、別宛なら exec_abort_clear + exec_kill)。ここを塞いだ版では
     * 連鎖の末尾が端末自身のとき (3 回目の CTRL+STOP) 誰も畳まなかった。 */
    ma_init(4096);
    a2 = ma_start(100, 1);
    check(a2 == APP_ID_MIN, "25G 端末が立つ");
    appslot_mark_scheduled(a2, 1000);
    ma_syscall_enter(1000);

    /* (g-1) OP_WAIT の外 (syscall から戻って CPL=3 で走っている) → 立てない */
    check(appslot_get(a2)->in_op_wait == 0, "25H まだ OP_WAIT の中ではない");
    check(ma_irq_abort_request(1, 1000) == 0,
          "25I CPL=3 で走っている最中の IRQ1 は立てない (D8 の宛先を待つ)");
    check(appslot_get(a2)->abort_req == 0, "25J abort_req は立たない");

    /* (g-2) gui_call(OP_WAIT) の中 → 立てる (K5c) */
    ma_gui_call(MA_OP_WAIT);                 /* appslot_gui_op_enter(1) */
    check(appslot_get(a2)->in_op_wait == 1, "25K OP_WAIT の中に居る");
    check(ma_irq_abort_request(1, 1000) == 1,
          "25L OP_WAIT の中の IRQ1 は従来どおり立てる (K5c)");
    check(appslot_get(a2)->abort_req == 1, "25M 走っているアプリに載る");
    /* WM が宛先を解決する: 本人宛なら降ろさず syscall 出口で畳ませる */
    check(ma_abort_check() == 0 && appslot_get(a2) == 0,
          "25N 本人宛ならそのまま畳まれる (連鎖の末尾 = 自分自身)");

    /* (g-3) 別宛なら WM が exec_abort_clear で降ろせる (K5c の分岐) */
    ma_init(4096);
    a2 = ma_start(100, 1);
    appslot_mark_scheduled(a2, 1000);
    ma_syscall_enter(1000);
    ma_gui_call(MA_OP_WAIT);
    check(ma_irq_abort_request(1, 1000) == 1, "25O OP_WAIT 中に立つ");
    {
        int owner0 = res_owner_get();
        res_owner_set(APP_ID_SHELL);         /* WM のハンドラの文脈 */
        check(ma_abort_clear() == 0, "25P WM が exec_abort_clear で降ろせる");
        res_owner_set(owner0);
    }
    check(appslot_get(a2)->abort_req == 0, "25Q 要求は降りている");
    check(ma_abort_check() == 0 && appslot_get(a2) != 0,
          "25R 本人は畳まれない (WM が別の宛先を exec_kill する)");
}

static void case_fullscreen_resume_destination(void)
{
    int sh, y;
    ma_init(4096);
    kbd_inject_discard();
    appslot_poll_yield_reset();
    host_reader = APP_ID_MIN;
    sh = ma_start_gfx(100, 1, 0);
    check(ma_park_kbd() == 0, "F1 hidden shell WAIT_KEY");
    y = ma_start_gfx(100, 1, OS32X_FLAG_GFX);
    check(appslot_gfx_claim(1) == 0, "F1 Y owns fullscreen");
    check(ma_park_poll(1) == 0, "F1 Y WAIT_POLL");
    check(kbd_inject((const u8 *)"q", 1) == 1, "F1 WM injects q");
    check(kbd_inject_pending() == 0, "F1 WM legacy pending excludes Y");
    check(ma_resume_kbd(sh) == OS32_ERR_AGAIN, "F1 hidden shell resume cannot take q");
    check(appslot_get(sh)->parked_from_kbd == 1 && appslot_cur() == APP_ID_SHELL,
          "F1 refused shell keeps park mark and WM context");
    check(ma_resume_poll(y) == 0 && appslot_get(y)->frame[APP_FRAME_EAX] == 'q',
          "F1 real WAIT_POLL branch delivers only to Y");
    check(ma_park_kbd() == 0, "F1 Y WAIT_KEY");
    check(kbd_inject((const u8 *)"r", 1) == 1, "F1 WM injects r");
    check(ma_resume_kbd(sh) == OS32_ERR_AGAIN, "F1 shell still cannot take r");
    check(ma_resume_kbd(y) == 0 && appslot_get(y)->frame[APP_FRAME_EAX] == 'r',
          "F1 real WAIT_KEY branch delivers only to Y");
    ma_init(4096);
    kbd_inject_discard();
}

int main(void)
{
    failures = 0;
    checks = 0;
    report("multiapp impl (K5b-K, exec/appslot.c)\n");
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
    case_resume_needs_wait_mark();
    case_shell_never_takes_app_band();

    case_abort_clear();
    case_wait_key();
    case_gfx_screen_owner();
    case_cui_only_and_reject_kill();
    case_poll_yield();
    case_yield_and_kill_chain();
    case_redirect_context();
    case_abort_admit();
    case_fullscreen_resume_destination();
    if (checks < 84) {
        report("TOO FEW CHECKS (K5a の 84 検査を下回った)\n");
        die(1);
    }
    if (failures) {
        report("FAILURES\n");
        die(1);
    }
    report("ALL PASS\n");
    die(0);
    return 0;
}

void _start(void)
{
    main();
}
