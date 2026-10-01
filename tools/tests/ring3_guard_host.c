/* ========================================================================
 *  ring3_guard_host.c — WM の文脈では KAPI の出力検査を効かせない (実物で)
 *
 *  対象票: docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md (追補 2026-09-26)
 *  実行:   python3 -B tools/tests/test_ring3_guard.py
 *  記録:   tools/tests/ring3_guard_tdd.md
 *
 *  症状: GUI で filer.bin を起動すると窓が出ずに静かに消え、fault_kill_count
 *  が +1。ブレークで捕まえると wrap_mouse_poll の出力検査
 *  (ring3_user_ranges_writable) が偽で ring3_fault_kill。ポインタは gshell
 *  自身のスタック (0x37ea88、シェル帯) だった。
 *
 *  原因: WM (gshell、CPL=0) は**アプリの syscall の中で**走る (契約 T8) ので、
 *  その間 ring3_in_syscall = 1 のまま。3 つの門 (書き側 / 読み側 / tramp_copy)
 *  は ring3_in_syscall だけで「アプリ由来」と決めていたので、WM 自身の
 *  ポインタがアプリの出力先として PTE を見られ、シェル帯には USER が無いので
 *  拒否 → アプリが kill された。
 *
 *  直し: 「カーネルが WM のコードへ入っている深さ」(ring3_wm_depth) を
 *  gui_call のハンドラ / ポンプ / owner_exit の前後で数え、門の判定は
 *  ring3_guard_active(in_syscall, wm_depth) (exec/ring3_str.c) に寄せる。
 *
 *  ここで実物のまま #include して見るのは 3 つ:
 *    (1) exec/ring3_str.c  — 判定表 (in_syscall × wm_depth)
 *    (2) kernel/gui.c      — 入口の配線: ハンドラの**中**では深さが 1 以上、
 *                            戻った後は元どおり。入れ子の gui_call でも同じ。
 *                            owner_exit も同じ印を立てる。
 *    (3) fs/fd_redirect.c  — 門は「ポインタの由来」で決める (代行レビュー P2):
 *                            アプリが登録したバッファは WM の中 (深さ 1) でも
 *                            表を歩いて断る。WM が張ったバッファは素通し。
 *  kernel/gui.c の外部参照 (res_owner_get / appslot_gui_op_* / kbd_set_gui_mode
 *  / ime_set_render / kstrncpy / kmemset / ring3_wm_enter / ring3_wm_leave) は
 *  ここで贋物にする。ring3_wm_enter / leave の贋物は exec/exec.c の実装と
 *  同じ 2 行 (深さ +1 / 0 で止める -1)。
 *
 *  tools/tests/ring3_str_host.c と同じ様式 — ホスト ILP32 GNU89 で走らせ、
 *  libc は使わない (-nostdlib、Linux の int 0x80 で write/exit するだけ)。
 * ======================================================================== */

#include "types.h"

/* ---- カーネル帯の代わり ------------------------------------------------ */
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    while (i + 1 < n && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return dst;
}

void *kmemset(void *dst, int c, u32 n)
{
    u8 *p = (u8 *)dst;
    while (n--) *p++ = (u8)c;
    return dst;
}

/* res_owner_get / res_owner_set は実物の fs/fd_redirect.c (下で #include)。
 * 試験は res_owner_set で「いま走っている ID」を差し替える。 */

/* exec/appslot.c の OP_WAIT の控え — 呼ばれた回数だけ見る。 */
static int host_op_enter, host_op_leave;
void appslot_gui_op_enter(int is_wait) { (void)is_wait; host_op_enter++; }
void appslot_gui_op_leave(void) { host_op_leave++; }

void kbd_set_gui_mode(int on) { (void)on; }
/* kernel/ime.c の実物の代わり: 控えた表を覚えるだけ。初期値は「TVRAM 版」
 * の代わりの印 (NULL と区別する)。gui_ime_set_render の門の試験 (5) が読む。 */
static int host_tvram_mark;
static void *host_ime_render = &host_tvram_mark;
static int host_ime_set_calls;
void ime_set_render(void *r) { host_ime_set_calls++; host_ime_render = r; }

/* exec/exec.c の ring3_wm_depth / enter / leave と同じ振る舞い。
 * 深さは試験が読む。 */
volatile int ring3_wm_depth = 0;
static int host_enter_calls, host_leave_calls;
void ring3_wm_enter(void) { host_enter_calls++; ring3_wm_depth++; }
void ring3_wm_leave(void) { host_leave_calls++; if (ring3_wm_depth > 0) ring3_wm_depth--; }

#include "ring3_str.h"   /* ring3_guard_active (実体は下で #include する実物) */
#include "vfs.h"         /* 下の VFS の贋物の型 */

/* ---- exec/exec.c の門 (fs/fd_redirect.c が引く 5 本) --------------------
 * 表の歩き (ring3_user_ranges_writable_always) は「app_page の中で、
 * host_page_rw が立っていれば書ける」だけの贋物。ページ属性が登録の後で
 * RO に変わる (sys_shm_lock / shlib .text) のは host_page_rw = 0 で装う。
 * 文脈つきの門と由来の問い合わせは exec/exec.c と同じ式 — 判定は実物の
 * ring3_guard_active (exec/ring3_str.c)。ring3_in_syscall は host_in_syscall。 */
static u8  app_page[64];              /* アプリのユーザ帯の 1 ページの代わり */
static u8  wm_buf[64];                /* シェル帯 (WM のバッファ) の代わり */
static int host_page_rw = 1;
static int host_in_syscall = 0;
static int host_walks, host_kills;

static int in_app_page(u32 p, u32 len)
{
    u32 lo = (u32)app_page, hi = (u32)app_page + (u32)sizeof(app_page);
    if (len == 0) return 1;
    return p >= lo && p + len <= hi && p + len > p;
}

int ring3_ptr_ok(u32 p)
{
    return p >= (u32)app_page && p < (u32)app_page + (u32)sizeof(app_page);
}

int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb)
{
    host_walks++;
    return host_page_rw && in_app_page(pa, la) && in_app_page(pb, lb);
}

int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb)
{
    if (!ring3_guard_active(host_in_syscall, ring3_wm_depth)) return 1;
    return ring3_user_ranges_writable_always(pa, la, pb, lb);
}

int ring3_call_from_user(void)
{
    return ring3_guard_active(host_in_syscall, ring3_wm_depth);
}

/* 実物は戻らない (longjmp)。ホストでは数えて戻る — 呼ばれたかだけを見る。 */
void ring3_fault_kill(void) { host_kills++; }

/* fs/fd_redirect.c が引く VFS (ファイル型は使わない)。 */
int vfs_open(const char *path, int mode) { (void)path; (void)mode; return -1; }
void vfs_close(int fd) { (void)fd; }
int vfs_seek(int fd, int off, int whence) { (void)fd; (void)off; (void)whence; return 0; }
int vfs_read_fd(int fd, void *buf, u32 size) { (void)fd; (void)buf; (void)size; return -1; }
int vfs_write_fd(int fd, const void *buf, u32 size) { (void)fd; (void)buf; (void)size; return -1; }

/* 実物 */
#include "ring3_str.c"
#include "gui.c"
#include "fd_redirect.c"
#include "redir_access_stub.h"

/* ---- 最小の報告系 (libc 無し) ------------------------------------------ */

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

static void check(int cond, const char *name)
{
    report(cond ? "  ok   " : "  FAIL ");
    report(name);
    report("\n");
    if (!cond) failures++;
}

/* ========================================================================
 *  1. 判定表 — ring3_guard_active(in_syscall, wm_depth)
 * ======================================================================== */
static void case_table(void)
{
    report("1 ring3_guard_active の表\n");

    /* ディスパッチの外 (CPL=0 の直呼び) は深さに関係なく対象外 (従来どおり)。 */
    check(ring3_guard_active(0, 0) == 0, "1a CPL0 direct, depth 0 -> off");
    check(ring3_guard_active(0, 1) == 0, "1b CPL0 direct, depth 1 -> off");

    /* ディスパッチの中、WM に入っていない = アプリ由来 → ガード (従来どおり)。 */
    check(ring3_guard_active(1, 0) == 1, "1c in syscall, depth 0 -> ON");

    /* **これが直した点**: ディスパッチの中でも WM の文脈なら常駐側の直呼び扱い。 */
    check(ring3_guard_active(1, 1) == 0, "1d in syscall, in WM -> off");
    check(ring3_guard_active(1, 2) == 0, "1e in syscall, nested WM -> off");

    /* 壊れた深さ (負) は安全側 = ガード。 */
    check(ring3_guard_active(1, -1) == 1, "1f negative depth -> ON (safe side)");
}

/* ========================================================================
 *  2. gui_call の配線 — ハンドラの中では深さ 1 以上、戻ったら元どおり
 * ======================================================================== */
static int seen_depth_in_handler = -99;
static int seen_depth_nested = -99;
static int nest_once;
static u32 last_op;

static i32 host_handler(u32 op, u32 arg, int owner)
{
    (void)arg;
    (void)owner;
    last_op = op;
    seen_depth_in_handler = ring3_wm_depth;
    if (nest_once) {
        /* WM のハンドラの中から gui_call (入れ子)。深さは 2 になり、
         * 戻っても 1 のまま (このハンドラを抜けるまで WM の文脈)。 */
        nest_once = 0;
        (void)gui_call(GUI_OP_POLL, 0);
        seen_depth_nested = ring3_wm_depth;
    }
    return 7;
}

static i32 nested_probe(u32 op, u32 arg, int owner)
{
    (void)op; (void)arg; (void)owner;
    seen_depth_nested = ring3_wm_depth;
    return 0;
}

static void case_gui_call(void)
{
    i32 r;

    report("2 gui_call の前後で深さが対になる\n");

    /* 未登録なら NOSYS で、印も立てない。 */
    check(gui_call(GUI_OP_WAIT, 0) == OS32_ERR_NOSYS, "2a no WM -> NOSYS");
    check(host_enter_calls == 0 && ring3_wm_depth == 0, "2b no WM -> no mark");

    /* シェル帯 (owner 1) からの登録。 */
    res_owner_set(1);
    check(gui_register((void *)host_handler, (void *)0) == 0, "2c register from owner 1");

    /* アプリ (owner 2) の gui_call: ハンドラの中は深さ 1、戻ると 0。 */
    res_owner_set(2);
    r = gui_call(GUI_OP_WAIT, 5);
    check(r == 7, "2d handler result passes through");
    check(last_op == GUI_OP_WAIT, "2e op passes through");
    check(seen_depth_in_handler == 1, "2f depth is 1 inside the handler");
    check(ring3_wm_depth == 0, "2g depth is back to 0 after the handler");
    check(host_enter_calls == 1 && host_leave_calls == 1, "2h enter/leave paired once");
    /* 既存の OP_WAIT の控え (appslot) はそのまま対で呼ばれる。 */
    check(host_op_enter == 1 && host_op_leave == 1, "2i appslot op enter/leave untouched");

    /* 入れ子: ハンドラの中の gui_call で 2、戻って 1、抜けて 0。 */
    nest_once = 1;
    seen_depth_in_handler = -99;
    r = gui_call(GUI_OP_COMMIT, 0);
    check(r == 7, "2j nested call returns");
    check(seen_depth_nested == 1, "2k after nested gui_call depth is 1 (still in WM)");
    check(ring3_wm_depth == 0, "2l depth 0 after the outer handler");
    check(host_enter_calls == 3 && host_leave_calls == 3, "2m enter/leave paired for both levels");

    /* 入れ子の中の深さそのものは 2。 */
    res_owner_set(1);
    check(gui_register((void *)nested_probe, (void *)0) == 0, "2n re-register probe");
    ring3_wm_enter();                       /* 外側の WM 文脈を装う */
    (void)gui_call(GUI_OP_POLL, 0);
    check(seen_depth_nested == 2, "2o depth is 2 inside a nested handler");
    ring3_wm_leave();
    check(ring3_wm_depth == 0, "2p back to 0");
}

/* ========================================================================
 *  3. owner_exit — exec_exit からの回収通知も WM の文脈として印を立てる
 * ======================================================================== */
static int exit_depth = -99;
static int exit_owner = -99;
static i32 exit_handler(u32 op, u32 arg, int owner)
{
    (void)arg;
    if (op == GUI_OP_OWNER_EXIT) {
        exit_depth = ring3_wm_depth;
        exit_owner = owner;
    }
    return 0;
}

static void case_owner_exit(void)
{
    int before_enter, before_leave;

    report("3 gui_owner_exit の前後で深さが対になる\n");

    res_owner_set(1);
    check(gui_register((void *)exit_handler, (void *)0) == 0, "3a register");
    before_enter = host_enter_calls;
    before_leave = host_leave_calls;

    gui_owner_exit(3);                      /* アプリ (owner 3) の回収 */
    check(exit_depth == 1 && exit_owner == 3, "3b OWNER_EXIT runs with depth 1");
    check(ring3_wm_depth == 0, "3c depth back to 0");
    check(host_enter_calls == before_enter + 1 &&
          host_leave_calls == before_leave + 1, "3d enter/leave paired");

    /* WM 自身 (owner 1) の終了 = 登録解除。以後の gui_call は印を立てない。 */
    gui_owner_exit(GUI_SHELL_OWNER);
    before_enter = host_enter_calls;
    check(gui_call(GUI_OP_POLL, 0) == OS32_ERR_NOSYS, "3e WM gone -> NOSYS");
    check(host_enter_calls == before_enter && ring3_wm_depth == 0,
          "3f WM gone -> no mark");
    /* 未登録の owner_exit は何もしない (印も立てない)。 */
    gui_owner_exit(2);
    check(host_enter_calls == before_enter, "3g owner_exit without WM -> no mark");
}

/* ========================================================================
 *  4. 門はポインタの由来で決める — アプリが登録したバッファは WM の中でも歩く
 *     (2026-09-26、代行レビュー P2。fs/fd_redirect.c の最後の砦)
 *
 *  筋書き: アプリ (ID 2) が fd 1 を自分のページへリダイレクト → そのページが
 *  RO になる (sys_shm_lock 等) → gui_call(OP_WAIT) → WM のハンドラ (深さ 1)
 *  が fd 1 へ書く。直す前は深さ 1 で門が素通しになり、CR0.WP=0 のカーネルが
 *  アプリの指定した番地へ書いていた。
 * ======================================================================== */
static int wm_wrote_rc = -99;
static int wm_seen_depth = -99;
static i32 wm_writes_fd1(u32 op, u32 arg, int owner)
{
    (void)op; (void)arg; (void)owner;
    wm_seen_depth = ring3_wm_depth;
    wm_wrote_rc = fd_redirect_write(1, "Z", 1);
    return 0;
}

static void case_registered_ptr(void)
{
    int kills0, walks0;

    report("4 アプリが登録したバッファは WM の中でも表を歩く\n");
    fd_redirect_init();
    ring3_wm_depth = 0;

    /* 4a-4c 登録: アプリの syscall (深さ 0) から自分のページを張る → 由来はアプリ。 */
    res_owner_set(2);
    host_in_syscall = 1;
    host_page_rw = 1;
    walks0 = host_walks;
    check(fd_redirect_to_buffer(1, app_page, 16u, 0) == 0, "4a app registers its page");
    check(redir_table[1].access.origin == 1 && redir_table[1].owner == 2,
          "4b entry is marked app-origin (owner 2)");
    check(host_walks == walks0 + 1, "4c registration walks the table");

    /* 4d-4g WM の文脈 (gui_call のハンドラ、深さ 1) で fd 1 へ書く。ページは RO。 */
    /* WM の登録は gshell の top-level (ディスパッチの外) から (§6 の門)。 */
    res_owner_set(1);
    host_in_syscall = 0;
    check(gui_register((void *)wm_writes_fd1, (void *)0) == 0, "4d WM registers");
    res_owner_set(2);
    host_in_syscall = 1;
    host_page_rw = 0;                      /* 登録の後で RO になった */
    kills0 = host_kills;
    walks0 = host_walks;
    (void)gui_call(GUI_OP_WAIT, 0);
    check(wm_seen_depth == 1, "4e the write runs with depth 1 (WM context)");
    check(host_walks == walks0 + 1, "4f depth 1 still walks the table (always)");
    check(host_kills == kills0 && wm_wrote_rc == -1, "4g RO app page is refused even inside WM");

    /* 4h 同じ筋書きでページが書けるなら素通しに書ける (過剰に殺さない)。 */
    host_page_rw = 1;
    kills0 = host_kills;
    (void)gui_call(GUI_OP_WAIT, 0);
    check(host_kills == kills0 && wm_wrote_rc == 1 && app_page[0] == 'Z',
          "4h writable app page is written inside WM");

    /* 4i 深さ 0 (アプリ自身の syscall) の RO も従来どおり断る。 */
    host_page_rw = 0;
    kills0 = host_kills;
    check(fd_redirect_write(1, "Q", 1) == -1, "4i error returned");
    check(host_kills == kills0, "4i depth 0 RO app page is refused (unchanged)");

    /* 4j-4k 登録時の二重の守り: アプリが RO / 帯外のページを張ろうとすると断り、
     * 表は変えない。 */
    host_page_rw = 0;
    check(fd_redirect_to_buffer(2, app_page, 16u, 0) == -1, "4j app cannot register RO page");
    check(redir_table[2].target_type == FD_TARGET_CONSOLE, "4k refused registration leaves table");
    host_page_rw = 1;
    check(fd_redirect_to_buffer(2, wm_buf, 16u, 0) == -1, "4l app cannot register non-user page");

    /* 4m-4o WM (深さ 1) が自分のバッファを張って書くのは素通し (由来は WM)。 */
    ring3_wm_enter();
    walks0 = host_walks;
    kills0 = host_kills;
    check(fd_redirect_to_buffer(2, wm_buf, 16u, 0) == 0 &&
          redir_table[2].access.origin == 0, "4m WM registers its own buffer (not app-origin)");
    check(fd_redirect_write(2, "W", 1) == 1 && wm_buf[0] == 'W' &&
          host_kills == kills0, "4n WM buffer written inside WM");
    check(host_walks == walks0, "4o WM buffer is not walked");
    ring3_wm_leave();

    /* 4p 退避と復帰 (park / resume) で由来の印が落ちない。 */
    {
        FdRedirectState st;
        fd_redirect_save(&st);
        check(st.fd[1].access.origin == 1, "4p save keeps app-origin mark");
        fd_redirect_restore(&st);
        check(redir_table[1].access.origin == 1, "4q restore keeps app-origin mark");
    }

    /* 4r 解除すると印も落ちる。 */
    fd_redirect_reset(1);
    check(redir_table[1].access.origin == 0, "4r reset clears the mark");

    host_in_syscall = 0;
    res_owner_set(1);
    gui_owner_exit(GUI_SHELL_OWNER);
}

/* ========================================================================
 *  5. ime_set_render の門 — 常駐側 (owner 1、CPL=0 の直呼び) だけが登録できる
 *     (2026-09-26、代行レビュー P2。kernel/gui.c の gui_ime_set_render)
 *
 *  カーネルは控えた IME_Render 表の関数を以後ずっと CPL=0 で呼ぶ。アプリが
 *  自分のメモリの表を渡せると、そのコードが CPL=0 で走り、アプリが消えた
 *  後は解放済みの物理へ飛ぶ。
 * ======================================================================== */
static u32 app_render_table[4];   /* アプリのユーザ帯の関数表の代わり */
static u32 wm_render_table[4];    /* gshell (シェル帯) の関数表の代わり */

static void case_ime_render(void)
{
    u32 rej0;

    report("5 ime_set_render は常駐側だけ\n");
    ring3_wm_depth = 0;

    /* 5a-5b gshell の top-level (owner 1、ディスパッチの外) の登録は通る。 */
    res_owner_set(1);
    host_in_syscall = 0;
    rej0 = gui_ime_render_rejected;
    gui_ime_set_render(wm_render_table);
    check(host_ime_render == (void *)wm_render_table, "5a owner 1 CPL0 registers");
    check(gui_ime_render_rejected == rej0, "5b no rejection counted");

    /* 5c-5e アプリ (owner 2、CPL=3 の syscall) の登録は断り、表は変えない。 */
    res_owner_set(2);
    host_in_syscall = 1;
    gui_ime_set_render(app_render_table);
    check(host_ime_render == (void *)wm_render_table, "5c app table refused (render unchanged)");
    check(gui_ime_render_rejected == rej0 + 1, "5d rejection counted");
    /* アプリが NULL で WM の描画先を外すこともさせない。 */
    gui_ime_set_render((void *)0);
    check(host_ime_render == (void *)wm_render_table, "5e app NULL refused");

    /* 5f owner 1 のままでも CPL=3 の syscall 由来 (ring3_call_from_user 真) は断る。 */
    res_owner_set(1);
    host_in_syscall = 1;
    gui_ime_set_render(app_render_table);
    check(host_ime_render == (void *)wm_render_table, "5f owner 1 but from user -> refused");

    /* 5g アプリ (owner 2) の syscall の中の WM の文脈 (深さ 1) も owner が違うので断る
     * (gshell はそこで ime_set_render を呼ばない)。 */
    res_owner_set(2);
    ring3_wm_enter();
    gui_ime_set_render(app_render_table);
    ring3_wm_leave();
    check(host_ime_render == (void *)wm_render_table, "5g owner 2 inside WM -> refused");

    /* 5h 常駐側の NULL (登録の解除) は通る。 */
    res_owner_set(1);
    host_in_syscall = 0;
    gui_ime_set_render((void *)0);
    check(host_ime_render == (void *)0, "5h owner 1 CPL0 NULL passes");

    /* 5i-5k 寿命: gshell (owner 1) の終了で TVRAM 版 (NULL) へ戻る。
     * アプリ (owner 3) の回収では触らない。 */
    gui_ime_set_render(wm_render_table);
    res_owner_set(0);                        /* exec_exit はシェルの回収で owner 0 */
    gui_owner_exit(3);
    check(host_ime_render == (void *)wm_render_table, "5i app exit leaves render");
    gui_owner_exit(GUI_SHELL_OWNER);
    check(host_ime_render == (void *)0, "5j shell exit resets render to NULL");
    res_owner_set(1);
    check(gui_ime_render_rejected == rej0 + 4, "5k four refusals counted");
}

/* ========================================================================
 *  6. gui_register の門 — ime_set_render と同じ「owner 1 かつ CPL=0 の直呼び」
 *     (2026-09-26、代行レビュー P3。kernel/gui.c の gui_register)
 *
 *  登録したハンドラ / ポンプは以後ずっと CPL=0 で呼ばれる。正当な呼び手は
 *  gshell の top-level (ディスパッチの外) だけで、それが通ることを固定する。
 * ======================================================================== */
static i32 wm_handler_a(u32 op, u32 arg, int owner)
{
    (void)op; (void)arg; (void)owner;
    return 11;
}

static i32 app_handler_b(u32 op, u32 arg, int owner)
{
    (void)op; (void)arg; (void)owner;
    return 22;
}

static void case_register_gate(void)
{
    report("6 gui_register は常駐側だけ\n");
    ring3_wm_depth = 0;

    /* 6a-6b gshell の top-level (owner 1、ディスパッチの外) の登録は通る。 */
    res_owner_set(1);
    host_in_syscall = 0;
    check(gui_register((void *)wm_handler_a, (void *)0) == 0, "6a owner 1 CPL0 registers");
    res_owner_set(2);
    check(gui_call(GUI_OP_POLL, 0) == 11, "6b registered WM handler receives gui_call");

    /* 6c-6d owner 1 のままでも CPL=3 の syscall 由来 (ring3_call_from_user 真) は断り、
     * 登録済みのハンドラは変えない。 */
    res_owner_set(1);
    host_in_syscall = 1;
    check(gui_register((void *)app_handler_b, (void *)0) == OS32_ERR_INVAL,
          "6c owner 1 but from user -> INVAL");
    res_owner_set(2);
    check(gui_call(GUI_OP_POLL, 0) == 11, "6d handler unchanged after refusal");

    /* 6e アプリ (owner 2) の syscall からは従来どおり断る。 */
    check(gui_register((void *)app_handler_b, (void *)0) == OS32_ERR_INVAL,
          "6e owner 2 from user -> INVAL");

    /* 6f アプリの syscall の中の WM の文脈 (深さ 1) でも owner が違えば断る。 */
    ring3_wm_enter();
    check(gui_register((void *)app_handler_b, (void *)0) == OS32_ERR_INVAL,
          "6f owner 2 inside WM -> INVAL");
    ring3_wm_leave();

    /* 6g NULL は常駐側でも断る (従来どおり)。 */
    res_owner_set(1);
    host_in_syscall = 0;
    check(gui_register((void *)0, (void *)0) == OS32_ERR_INVAL, "6g NULL handler -> INVAL");

    gui_owner_exit(GUI_SHELL_OWNER);
}

int main(void)
{
    case_table();
    case_gui_call();
    case_owner_exit();
    case_registered_ptr();
    case_ime_render();
    case_register_gate();
    if (failures) {
        report("ring3_guard_host: FAIL\n");
        die(1);
    }
    report("ring3_guard_host: all passed\n");
    return 0;
}

/* -nostdlib のエントリ。main の戻りで exit する。 */
void _start(void)
{
    int rc = main();
    die(rc);
}
