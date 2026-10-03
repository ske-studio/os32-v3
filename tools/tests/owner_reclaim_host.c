/* ========================================================================
 *  owner_reclaim_host.c — 所有者 ID による回収が **その ID の分だけ**で
 *                         閉じることを実物のソースで確かめる
 *
 *  対象票: docs/archive/gui_v13/TASK_K5B_kernel.md (ホスト試験の 2 本目)
 *  設計:   TASK_K5_multiapp.md D3 (`*_owned(id)` の棚卸しと P3/P5)
 *  実行:   python3 -B tools/tests/test_owner_reclaim.py
 *  記録:   tools/tests/k5b_kernel_tdd.md
 *
 *  アプリが 4 本同時に生きると、exec_exit の回収 7 種のうち **所有者を見て
 *  いなかった 2 種** (SHM とサウンド) が他のアプリの資源を巻き上げる。
 *  ここでは実物の fs/fd_redirect.c / fs/pipe_buffer.c / kernel/shm.c を
 *  そのままコンパイルし、ID 2 と ID 3 が資源を持った状態で ID 2 だけを
 *  畳んで「2 の分だけ」消えることを見る。
 *
 *  FD (fs/vfs_fd.c の vfs_close_owned) は tools/tests/test_vfs_fd_sqlite.py が、
 *  DB (kapi/kapi_db.c の db_cleanup_owned) は tools/tests/test_kapi_db_owned.py
 *  が既に同じ形で見ているので、ここでは重複させない。
 * ======================================================================== */
#include <stdio.h>
#include <string.h>
#include "types.h"
#include "vfs.h"
#include "paging.h"

/* ---- 実物が要る最小限のカーネル環境 --------------------------------
 * res_owner_set/get は fs/fd_redirect.c の実物をそのまま使う (この試験の
 * 主題そのものなので差し替えない)。 */

/* paging: shm.c はブロックの R/W 属性を張り替えるだけ。回数を数えて、
 * 「触られたブロック」がその ID のものだけであることを見る。 */
static unsigned int g_map_calls;
static u32 g_last_map_start;
int paging_map_range(u32 vs, u32 ve, u32 ps, u32 flags)
{
    (void)ve; (void)ps; (void)flags;
    g_map_calls++;
    g_last_map_start = vs;
    return 0;
}
int paging_shm_set_rw(u32 base, u32 end, int writable)
{
    return paging_map_range(base, end, base, writable ? PAGE_RW : PAGE_RO);
}
int paging_set_not_present(u32 s, u32 e)
{
    (void)s; (void)e;
    return 0;
}

/* pipe_buffer.c は kmalloc/kfree を使う */
static int g_live_allocs;
void *kmalloc(u32 size)
{
    static char pool[4][1024];
    static int next;
    (void)size;
    if (next >= 4) return 0;
    g_live_allocs++;
    return pool[next++];
}
void kfree(void *p) { if (p) g_live_allocs--; }

/* fs/fd_redirect.c の書き込み時の再検査 (票 TASK_KAPI_OUTPUT_GUARD) が引く exec/exec.c
 * の 5 本。ホストではユーザ帯の番地は無いので ring3_ptr_ok は常に 0 (= 再検査を通らない)。 */
int ring3_ptr_ok(u32 p) { (void)p; return 0; }
int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb)
{ (void)pa; (void)la; (void)pb; (void)lb; return 1; }
/* 由来つきの門 (2026-09-26): ここの呼び手は CPL=0 扱い (由来はアプリでない)。
 * アプリ由来の筋書きは tools/tests/ring3_guard_host.c が見る。 */
int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb)
{ (void)pa; (void)la; (void)pb; (void)lb; return 1; }
int ring3_call_from_user(void) { return 0; }
void ring3_fault_kill(void) { for (;;) { } }

/* fd_redirect.c が呼ぶ VFS。ここでは「開いた FD の台帳」だけ持つ。 */
static int g_vfs_open_fds;
int vfs_open(const char *path, int mode) { (void)path; (void)mode;
                                           g_vfs_open_fds++; return 10 + g_vfs_open_fds; }
void vfs_close(int fd) { (void)fd; g_vfs_open_fds--; }
int vfs_seek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; return 0; }
int vfs_read_fd(int fd, void *buf, u32 size) { (void)fd; (void)buf; (void)size; return 0; }
int vfs_write_fd(int fd, const void *buf, u32 size) { (void)fd; (void)buf; return (int)size; }

/* SHM 帯そのものは実メモリを持たない (実機の 0x1xxxxx を触れないので)。
 * shm.c が確保時に行うゼロクリアはここで記録するだけにする — この試験が
 * 見るのは所有者タグと状態遷移であって、帯の中身ではない。 */
static unsigned int g_zero_calls;
static unsigned int g_last_zero_start;
void test_shm_memset(void *d, int c, unsigned long n)
{
    (void)c; (void)n;
    g_zero_calls++;
    g_last_zero_start = (unsigned int)(unsigned long)d;
}

#include "../../fs/fd_redirect.c"
#include "redir_access_stub.h"
#include "../../fs/pipe_buffer.c"
#include "../../kernel/shm.c"

static int failures;
static int checks;

static void check(int cond, const char *name)
{
    checks++;
    if (cond) {
        printf("  ok   %s\n", name);
    } else {
        printf("  FAIL %s\n", name);
        failures++;
    }
}

/* exec_exit / exec_kill の回収の並び (exec/exec.c の exec_reclaim_owned)。
 * ここで見るのは所有者付きの 3 種。 */
static void reclaim_owned(int id)
{
    fd_redirect_reset_owned(id);
    pipe_free_owned(id);
    shm_free_owned(id);
}

int main(void)
{
    void *a2, *a3;
    int p2, p3;

    shm_init();
    pipe_buffer_init();
    fd_redirect_init();

    /* ---- ID 2 が資源を確保 ---- */
    res_owner_set(2);
    check(fd_redirect_to_file(1, "/tmp/two.txt", FD_REDIR_WRITE) == 0,
          "1a ID 2 が stdout をリダイレクトできる");
    p2 = pipe_alloc();
    check(p2 >= 0, "1b ID 2 がパイプバッファを取れる");
    g_zero_calls = 0;
    a2 = shm_alloc(2);
    check(a2 != 0, "1c ID 2 が SHM を 2 ブロック取れる");
    check(g_zero_calls == 1 && g_last_zero_start == (unsigned int)MEM_SHM_BASE,
          "1d 確保時に先頭からゼロクリアされる (前の所有者のデータを見せない)");

    /* ---- ID 3 が資源を確保 ---- */
    res_owner_set(3);
    p3 = pipe_alloc();
    check(p3 >= 0 && p3 != p2, "2a ID 3 が別のパイプバッファを取れる");
    a3 = shm_alloc(1);
    check(a3 != 0 && a3 != a2, "2b ID 3 が別の SHM ブロックを取れる");
    check(shm_state[0] == SHM_USED && shm_state[1] == SHM_USED &&
          shm_state[2] == SHM_USED,
          "2c 3 ブロックが使用中 (2 の 2 枚 + 3 の 1 枚)");
    check(shm_block_owner[0] == 2 && shm_block_owner[1] == 2 &&
          shm_block_owner[2] == 3,
          "2d SHM ブロックに確保した ID のタグが付く");

    /* ---- ID 2 だけを畳む ---- */
    g_map_calls = 0;
    reclaim_owned(2);

    check(shm_state[0] == SHM_FREE && shm_state[1] == SHM_FREE,
          "3a ID 2 の SHM ブロックだけが空く");
    check(shm_state[2] == SHM_USED && shm_block_owner[2] == 3,
          "3b ID 3 の SHM ブロックは使用中のまま");
    check(g_map_calls == 2, "3c 属性を戻したのは ID 2 の 2 ブロックだけ");
    check(pipe_used[p2] == 0, "3d ID 2 のパイプバッファが返る");
    check(pipe_used[p3] == 1 && pipe_owner[p3] == 3,
          "3e ID 3 のパイプバッファは残る");
    check(g_live_allocs == 1, "3f kfree されたのは 1 本だけ");
    check(fd_is_redirected(1) == 0, "3g ID 2 のリダイレクトが解ける");
    check(g_vfs_open_fds == 0, "3h リダイレクト先の FD も閉じる");

    /* ---- GUI 予約 (契約 T2) は誰の終了でも触らない ---- */
    check(shm_state[SHM_GUI_BLOCK_FIRST] == SHM_RESERVED,
          "4a GUI 予約ブロックは回収されない");
    res_owner_set(1);
    reclaim_owned(1);
    check(shm_state[SHM_GUI_BLOCK_FIRST] == SHM_RESERVED,
          "4b シェル帯の回収でも GUI 予約は無傷");
    check(shm_state[2] == SHM_USED,
          "4c 無関係な ID の SHM はシェルの回収でも残る");

    /* ---- 所有者 0 (タグなし) では何も回収しない ---- */
    reclaim_owned(0);
    check(shm_state[2] == SHM_USED, "5a owner 0 の回収は何も解放しない");

    /* ---- ID 3 を畳めば残りが返る ---- */
    reclaim_owned(3);
    check(shm_state[2] == SHM_FREE, "5b ID 3 の回収で残りの SHM が返る");
    check(pipe_used[p3] == 0 && g_live_allocs == 0,
          "5c ID 3 のパイプバッファも返る");
    check(shm_state[SHM_GUI_BLOCK_FIRST] == SHM_RESERVED,
          "5d 最後まで GUI 予約は無傷");

    /* ---- shm_free_owned は lock 済みブロックも返す ---- */
    res_owner_set(4);
    a2 = shm_alloc(1);
    check(a2 != 0 && shm_lock(a2) == 0, "6a ID 4 が SHM を lock できる");
    check(shm_state[0] == SHM_LOCKED, "6b lock 後の状態は SHM_LOCKED");
    reclaim_owned(4);
    check(shm_state[0] == SHM_FREE, "6c lock 済みでも所有者の回収で返る");
    check(shm_block_owner[0] == 0, "6d 返ったブロックのタグは消える");

    if (failures) {
        printf("FAILURES (%d/%d)\n", failures, checks);
        return 1;
    }
    printf("ALL PASS (%d checks)\n", checks);
    return 0;
}
