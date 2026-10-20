/* ========================================================================
 *  shell_service_host.c — 常駐 shell の内部入口の段 A / B と登録 (T7b-b2 S3a)
 *                         + exec/exec.c への結線 (S3b)
 *
 *  設計: ~/os32-tmp/run/t7bb12/DESIGN.final.md §3・§5 (b2①③⑤⑥)
 *  実行: python3 -B tools/tests/test_shell_service.py [--target] [--mutate]
 *
 *  実物 exec/shell_service.c と exec/ring3_str.c (ring3_guard_active) を
 *  #include する。exec/exec.c はカーネル一式を引くので丸ごとは組めない —
 *  試験 py が exec.c から結線の部分を切り出して tmp に書き、ここで #include
 *  する (S3b、手本 test_caller_access.py の wm.inc):
 *    wire_wm.inc     ring3_wm_enter / ring3_wm_leave (深さと WM の印)
 *    wire_ctx.inc    Ring3CallContext と ring3_context_save / clear / restore
 *    wire_commit.inc exec_run の shell 確定 (appslot_shell_commit → retire → publish)
 *    wire_finish.inc exec_finish の shell 分岐 (exec_reclaim_owned → retire)
 *  dispatch 入口の印の 0 と frame[5] は字面で見る (試験 py の static_checks)。
 *
 *  version 2 (T7b-b3 S2): 表の末尾の font_guard_run の wrapper (段 A/B) を
 *  6 で見る (x87 無しの open の NOSYS は TASK_FONTPERF F6 で撤去)。kernel/font_guard.c の実物は
 *  test_font_guard.py が見るので、ここでは贋物 (呼ばれた数・戻りを差し替え
 *  可能、既定は call(ctx) をそのまま呼ぶ) を置く。
 *
 *  段 C / D の実体 (kernel/font_source.c の font_lease_shell_*、S2) は
 *  別の試験 (font_source_host.c) が見るので、ここでは registry の贋物を置く
 *  (署名は kernel/font_source.h): lease は「使用中 + owner_gen」
 *  だけで、渡された shell_gen と違えば STALE、close_shell で全部 FREE。
 *
 *  戻り番地の範囲 (段 A) は、正当な呼び手を section "svc_in" / "svc_in2"
 *  に置き、ld が作る __start_* / __stop_* を publish の text 範囲にする
 *  (設計 §5 b2①: 試験用呼出し関数の番地範囲)。範囲外の呼び手は .text。
 *
 *  様式は ring3_guard_host.c と同じ — ホスト ILP32 GNU11、libc 無し
 *  (-nostdlib、Linux の int 0x80 で write/exit)。
 * ======================================================================== */

#include "types.h"
#include "appslot.h"
#include "exec.h"
#include "gui.h"
#include "fd_redirect.h"
#include "redir_access.h"   /* CallerAccessFrame (wire_ctx.inc の Ring3CallContext) */

/* ---- exec/exec.c・exec/appslot.c・fs/fd_redirect.c の代わり ------------- */
volatile int ring3_in_syscall = 0;
volatile int ring3_wm_depth = 0;
volatile u32 ring3_wm_depth_underflow = 0;
static int host_cur = APP_ID_SHELL;
static int host_owner = GUI_SHELL_OWNER;
static AppSlot host_slot;

int appslot_cur(void) { return host_cur; }
AppSlot *appslot_get(int id) { return id == APP_ID_SHELL ? &host_slot : (AppSlot *)0; }
int res_owner_get(void) { return host_owner; }

#include "ring3_str.h"
int ring3_call_from_user(void)
{
    return ring3_guard_active(ring3_in_syscall, ring3_wm_depth);
}

/* ring3_str.c が引く kstring (使わないが参照は解決する)。 */
char *kstrncpy(char *dst, const char *src, u32 n)
{
    u32 i = 0;
    if (n == 0) return dst;
    while (i + 1 < n && src[i] != '\0') { dst[i] = src[i]; i++; }
    dst[i] = '\0';
    return dst;
}

/* ---- 段 C / D の贋物 (kernel/font_source.c の shell 側実体の代わり) ------ */
#define FAKE_LEASES 4
static struct { int used; u32 owner_gen; } fake[FAKE_LEASES];
static u32 fake_last_gen;
static int fake_close_shell_calls;

static int fake_in_use(void)
{
    int i, n = 0;
    for (i = 0; i < FAKE_LEASES; i++) n += fake[i].used;
    return n;
}

i32 font_lease_shell_open(const char *path, u32 shell_gen, u32 *out_id, u32 *out_size)
{
    u32 i;
    (void)path;
    fake_last_gen = shell_gen;
    for (i = 0; i < FAKE_LEASES; i++) {
        if (!fake[i].used) {
            fake[i].used = 1;
            fake[i].owner_gen = shell_gen;
            *out_id = i;
            *out_size = 1234;
            return 0;
        }
    }
    return OS32_ERR_NOSPC;
}

static i32 fake_check(u32 id, u32 shell_gen)
{
    fake_last_gen = shell_gen;
    if (id >= FAKE_LEASES) return OS32_ERR_INVAL;
    if (!fake[id].used || fake[id].owner_gen != shell_gen) return OS32_ERR_STALE;
    return 0;
}

i32 font_lease_shell_read_at(u32 id, u32 shell_gen, u32 offset, void *dst, u32 len)
{
    i32 r = fake_check(id, shell_gen);
    u32 i;
    (void)offset;
    if (r) return r;
    for (i = 0; i < len; i++) ((u8 *)dst)[i] = (u8)'S';
    return (i32)len;
}

i32 font_lease_shell_validate(u32 id, u32 shell_gen) { return fake_check(id, shell_gen); }

i32 font_lease_shell_close(u32 id, u32 shell_gen)
{
    i32 r = fake_check(id, shell_gen);
    if (r) return r;
    fake[id].used = 0;
    return 0;
}

void font_lease_close_shell(void)
{
    int i;
    fake_close_shell_calls++;
    for (i = 0; i < FAKE_LEASES; i++) fake[i].used = 0;
}

/* ---- kernel/font_guard.c の贋物 (version 2 の wrapper の先) ------------ */
static int fake_guard_calls;
static i32 fake_guard_rc;          /* 0 なら call(ctx) を呼ぶ、他はそれを返す */
int font_guard_run(int (*call)(void *), void *context)
{
    fake_guard_calls++;
    if (fake_guard_rc) return fake_guard_rc;
    if (!call) return OS32_ERR_INVAL;
    return call(context);
}

/* 実物 */
#include "ring3_str.c"
#include "shell_service.c"

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
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(text), "d"(len) : "memory");
}

static int failures;

static void check(int cond, const char *name)
{
    report(cond ? "  ok   " : "  FAIL ");
    report(name);
    report("\n");
    if (!cond) failures++;
}

/* ---- 呼び手 ------------------------------------------------------------
 * 表の関数番地を通して呼び、戻り値を局所に受けてから返す (尾呼出しにしない、
 * 設計 §3-2 の b3 向け注記と同じ形)。 */
#define SVC_CALLERS(sec, sfx)                                                  \
static __attribute__((noinline, unused, section(sec))) i32 open_##sfx(u32 *id, u32 *size) \
{ volatile i32 r = g_shell_service.font_source_open("/sys/font", id, size); return r; } \
static __attribute__((noinline, unused, section(sec))) i32 read_##sfx(u32 id, u8 *dst, u32 len) \
{ volatile i32 r = g_shell_service.font_source_read_at(id, 0, dst, len); return r; } \
static __attribute__((noinline, unused, section(sec))) i32 validate_##sfx(u32 id)         \
{ volatile i32 r = g_shell_service.font_source_validate(id); return r; }        \
static __attribute__((noinline, unused, section(sec))) i32 close_##sfx(u32 id)            \
{ volatile i32 r = g_shell_service.font_source_close(id); return r; }        \
static __attribute__((noinline, unused, section(sec))) i32 guard_##sfx(int (*cb)(void *), void *ctx) \
{ volatile i32 r = g_shell_service.font_guard_run(cb, ctx); return r; }

SVC_CALLERS("svc_in", in)     /* 1 本目の shell の text */
SVC_CALLERS("svc_in2", in2)   /* reload 後の shell の text */
SVC_CALLERS(".text", out)     /* shell text の外 (kernel 等) */

extern char __start_svc_in[], __stop_svc_in[];
extern char __start_svc_in2[], __stop_svc_in2[];

static void publish_in(void)
{
    shell_service_publish((u32)(uptr)__start_svc_in,
                          (u32)(uptr)(__stop_svc_in - __start_svc_in));
}

static void publish_in2(void)
{
    shell_service_publish((u32)(uptr)__start_svc_in2,
                          (u32)(uptr)(__stop_svc_in2 - __start_svc_in2));
}

/* 常駐 shell の通常文脈 (int 0x80 の外、WM の外、CPL=0 の shell が走行中)。 */
static void plain_context(void)
{
    ring3_in_syscall = 0;
    ring3_wm_depth = 0;
    ring3_wm_shell_gen = 0;
    host_cur = APP_ID_SHELL;
    host_owner = GUI_SHELL_OWNER;
    host_slot.cpl3 = 0;
    host_slot.state = APP_STATE_RUNNING;
}

/* ---- exec/exec.c から切り出した結線 (S3b) ------------------------------ */
/* ring3_context_* が引く外部 (caller の控え・TSS・IRQ) の贋物。 */
static u32 *g_cur_frame = 0;
static u32 host_esp0;
static int host_caller_saves, host_caller_invalidates, host_caller_leaves;
void caller_access_save(volatile CallerAccessFrame *out) { (void)out; host_caller_saves++; }
void caller_access_invalidate(void) { host_caller_invalidates++; }
void caller_access_leave(const volatile CallerAccessFrame *previous)
{ (void)previous; host_caller_leaves++; }
u32 tss_get_esp0(void) { return host_esp0; }
void tss_set_esp0(u32 esp0) { host_esp0 = esp0; }
static unsigned int irq_save(void) { return 0; }
static void irq_restore(unsigned int flags) { (void)flags; }

/* exec_run / exec_finish の shell 分岐が呼ぶ外部の贋物。 */
static int host_commit_calls, host_reclaim_calls, host_reclaim_seen_close;
void appslot_shell_commit(void) { host_commit_calls++; }
static void exec_reclaim_owned(int id, int kind)
{
    (void)id; (void)kind;
    host_reclaim_calls++;
    host_reclaim_seen_close = fake_close_shell_calls;   /* retire はこの後のはず */
}

#include "wire_wm.inc"
#include "wire_ctx.inc"

/* exec_run の `if (is_shell) { appslot_shell_commit(); ... }` の中身。 */
static void host_shell_commit(u32 load_base, u32 text_sz)
{
#include "wire_commit.inc"
}

/* exec_finish の `if (id == APP_ID_SHELL)` 分岐の res_owner_set(0) より後。 */
static void host_shell_finish(int id, int kind)
{
#include "wire_finish.inc"
}

/* 実物の ring3_wm_enter / leave (exec/exec.c) を通す。 */
static void wm_enter(void) { ring3_wm_enter(); }
static void wm_leave(void) { ring3_wm_leave(); }

static int rejected_with(u32 before, u32 reason)
{
    return shell_service_reject_count == before + 1 && shell_service_reject_last == reason;
}

/* ========================================================================
 *  1. 表の登録 (publish / retire)
 * ======================================================================== */
static void case_publish(void)
{
    u32 g0 = g_shell_service.generation;
    report("1 publish / retire\n");
    check(g0 == 0, "1a 起動直後は未登録 (generation 0)");
    publish_in();
    check(g_shell_service.magic == SHELL_SERVICE_MAGIC &&
          g_shell_service.version == SHELL_SERVICE_VERSION, "1b magic / version");
    check(g_shell_service.generation == 1, "1c publish で generation 1");
    check(g_shell_service.font_source_open && g_shell_service.font_source_read_at &&
          g_shell_service.font_source_validate && g_shell_service.font_source_close &&
          g_shell_service.font_guard_run, "1d 関数 5 本が埋まる (version 2 の font_guard_run)");
    check(SHELL_SERVICE_VERSION == 2 && SHELL_SERVICE_SIZE == 32 &&
          sizeof(ShellService) == SHELL_SERVICE_SIZE &&
          __builtin_offsetof(ShellService, font_guard_run) == 28,
          "1d' version 2・SIZE 32・font_guard_run は末尾 (offset 28)");
    check(g_shell_active && g_shell_text_lo == (u32)(uptr)__start_svc_in &&
          g_shell_text_hi == (u32)(uptr)__stop_svc_in, "1e text 範囲を控える");
    shell_service_retire();
    check(!g_shell_active && g_shell_text_lo == 0 && g_shell_text_hi == 0,
          "1f retire で登録を外し範囲を 0 に");
    check(g_shell_service.generation == 1, "1g retire は generation を進めない");
    shell_service_publish(0x1000, 0);
    check(!g_shell_active && g_shell_service.generation == 2, "1h text_size 0 は登録しない");
    shell_service_publish(0xFFFFF000u, 0x2000);
    check(!g_shell_active && g_shell_text_hi == 0, "1i 桁あふれの範囲は登録しない");
    /* 以降の場面のため generation を 0 から数え直す (試験だけの都合)。 */
    g_shell_service.generation = 0;
}

/* ========================================================================
 *  2. b2① 段 A (戻り番地) / 段 B (文脈)
 * ======================================================================== */
static void case_gate(void)
{
    u32 id = 99, size = 0, c;
    u8 buf[4];
    report("2 b2① text / 文脈\n");
    plain_context();
    publish_in();
    check(open_in(&id, &size) == 0 && id == 0 && size == 1234, "2a shell text から open");
    check(fake_last_gen == g_shell_service.generation, "2b 段 C へ現 generation を渡す");
    check(read_in(id, buf, 4) == 4 && buf[0] == 'S', "2c 通常文脈の read_at");
    check(validate_in(id) == 0, "2d 通常文脈の validate");

    c = shell_service_reject_count;
    check(open_out(&id, &size) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT),
          "2e text の外からの open は INVAL");
    c = shell_service_reject_count;
    check(read_out(id, buf, 4) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT),
          "2f text の外からの read_at は INVAL");
    c = shell_service_reject_count;
    check(validate_out(id) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT),
          "2g text の外からの validate は INVAL");
    c = shell_service_reject_count;
    check(close_out(id) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT) &&
          fake_in_use() == 1, "2h text の外からの close は INVAL (閉じない)");

    /* 段 B: read は CPL=3 由来 (syscall の中で WM の外) を断る。 */
    ring3_in_syscall = 1;
    c = shell_service_reject_count;
    check(read_in(id, buf, 4) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_READ),
          "2i in_syscall=1 wm_depth=0 の read_at は INVAL");
    c = shell_service_reject_count;
    check(validate_in(id) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_READ),
          "2j in_syscall=1 wm_depth=0 の validate は INVAL");
    c = shell_service_reject_count;
    check(open_in(&id, &size) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_OPEN),
          "2k in_syscall=1 の open は INVAL");
    plain_context();

    /* 段 B: open / close は WM の中 (登録済みの印があっても) 不可。 */
    wm_enter();
    c = shell_service_reject_count;
    check(open_in(&id, &size) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_OPEN),
          "2l WM 中 (wm_depth=1) の open は INVAL");
    c = shell_service_reject_count;
    check(close_in(id) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_OPEN) &&
          fake_in_use() == 1, "2m WM 中の close は INVAL (閉じない)");
    wm_leave();

    /* 段 B: shell の通常文脈の各項 (exec_disk_write_allowed と同じイディオム)。 */
    host_cur = APP_ID_SHELL + 1;
    check(open_in(&id, &size) == OS32_ERR_INVAL, "2n 走っているのが shell でない");
    plain_context();
    host_slot.cpl3 = 1;
    check(open_in(&id, &size) == OS32_ERR_INVAL, "2o shell slot が CPL=3");
    plain_context();
    host_owner = GUI_SHELL_OWNER + 1;
    check(open_in(&id, &size) == OS32_ERR_INVAL, "2p res_owner が shell でない");
    plain_context();
    host_slot.state = APP_STATE_RUNNING + 1;
    check(open_in(&id, &size) == OS32_ERR_INVAL, "2q shell slot が RUNNING でない");
    plain_context();
    ring3_in_syscall = 1;
    ring3_wm_depth = -1;   /* 壊れた深さは安全側 */
    check(read_in(0, buf, 4) == OS32_ERR_INVAL, "2r 負の深さの read_at は INVAL");
    plain_context();

    check(close_in(0) == 0 && fake_in_use() == 0, "2s 通常文脈の close");

    /* 段 A: 登録されていなければ text の中でも断る。 */
    shell_service_retire();
    c = shell_service_reject_count;
    check(open_in(&id, &size) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_INACTIVE),
          "2t 未登録 (retire 後) の open は INVAL");
    c = shell_service_reject_count;
    check(read_in(0, buf, 4) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_INACTIVE),
          "2u 未登録の read_at は INVAL");
}

/* ========================================================================
 *  3. b2③ WM 中の shell read (登録済み handler / pump / owner_exit 区間)
 * ======================================================================== */
static void case_wm(void)
{
    u32 id = 99, size = 0, c;
    u8 buf[4];
    report("3 b2③ WM 中の read\n");
    plain_context();
    publish_in();
    check(open_in(&id, &size) == 0, "3a 通常文脈で open");

    /* アプリの syscall の中の handler 区間: in_syscall=1、owner はアプリ。 */
    ring3_in_syscall = 1;
    host_cur = APP_ID_SHELL + 1;
    host_owner = APP_ID_SHELL + 1;
    wm_enter();
    check(ring3_wm_shell_gen == g_shell_service.generation, "3b enter で印 = 現 generation");
    check(read_in(id, buf, 4) == 4, "3c 登録済み WM 区間の read_at は len");
    check(validate_in(id) == 0, "3d 登録済み WM 区間の validate");
    wm_enter();
    check(read_in(id, buf, 4) == 4, "3e 入れ子の WM 区間も通る");
    wm_leave();
    wm_leave();
    check(ring3_wm_shell_gen == 0, "3f 深さ 0 に戻れば印 0");

    /* 印の無い WM 区間 (深さだけ 1) は断る — 選言の片方だけでは通さない。 */
    ring3_wm_depth = 1;
    ring3_wm_shell_gen = 0;
    c = shell_service_reject_count;
    check(read_in(id, buf, 4) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_READ),
          "3g 印 0 の WM 区間の read_at は INVAL");
    /* 旧世代の印も断る。 */
    ring3_wm_shell_gen = g_shell_service.generation - 1;
    check(read_in(id, buf, 4) == OS32_ERR_INVAL, "3h 旧世代の印の read_at は INVAL");
    ring3_wm_depth = 0;
    ring3_wm_shell_gen = 0;

    /* 未登録のうちに入った WM 区間 → その中で publish → read は INVAL。 */
    plain_context();
    shell_service_retire();
    ring3_in_syscall = 1;
    host_cur = APP_ID_SHELL + 1;
    host_owner = APP_ID_SHELL + 1;
    wm_enter();
    check(ring3_wm_shell_gen == 0, "3i 未登録の enter は印を立てない");
    publish_in();
    check(read_in(0, buf, 4) == OS32_ERR_INVAL, "3j 未登録で入った区間は publish 後も INVAL");
    wm_leave();
    plain_context();
    shell_service_retire();
}

/* ========================================================================
 *  4. b2⑥ reload (retire → publish) で旧 lease・旧 text が無効
 * ======================================================================== */
static void case_reload(void)
{
    u32 id = 99, size = 0, gen1, c;
    u8 buf[4];
    report("4 b2⑥ reload\n");
    plain_context();
    g_shell_service.generation = 0;
    fake_close_shell_calls = 0;
    publish_in();
    gen1 = g_shell_service.generation;
    check(gen1 == 1, "4a 1 本目の shell は generation 1");
    check(open_in(&id, &size) == 0 && fake_in_use() == 1, "4b 旧 shell が open");
    shell_service_retire();
    check(fake_close_shell_calls == 1 && fake_in_use() == 0, "4c retire で shell lease を全部閉じる");
    publish_in2();
    check(g_shell_service.generation == 2, "4d 次の publish で generation 2 (直読)");
    check(read_in2(id, buf, 4) == OS32_ERR_STALE, "4e 旧 id の read_at は STALE");
    check(close_in2(id) == OS32_ERR_STALE, "4f 旧 id の close は STALE");
    c = shell_service_reject_count;
    check(read_in(id, buf, 4) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT),
          "4g 旧 text からの呼出しは INVAL");
    check(open_in2(&id, &size) == 0 && fake_last_gen == 2, "4h 新 shell の open は gen 2");
    check(read_in2(id, buf, 4) == 4, "4i 新 shell の read_at");
    shell_service_retire();
}

/* ========================================================================
 *  5. S3b: exec/exec.c の結線 (切り出した実物)
 * ======================================================================== */
static void case_wire(void)
{
    u32 id = 99, size = 0, gen;
    u8 buf[4];
    volatile Ring3CallContext saved;
    report("5 S3b exec.c の結線\n");
    plain_context();
    g_shell_service.generation = 0;
    shell_service_retire();

    /* (a) ring3_wm_enter / leave: 一番外の enter で印、深さ 0 で 0。 */
    publish_in();
    gen = g_shell_service.generation;
    ring3_wm_enter();
    check(ring3_wm_depth == 1 && ring3_wm_shell_gen == gen, "5a enter (深さ 1) で印 = 現 generation");
    ring3_wm_shell_gen = 77;
    ring3_wm_enter();
    check(ring3_wm_depth == 2 && ring3_wm_shell_gen == 77, "5b 入れ子の enter は印を書き換えない");
    ring3_wm_leave();
    check(ring3_wm_depth == 1 && ring3_wm_shell_gen == 77, "5c 深さ 1 に戻る leave は印を残す");
    ring3_wm_leave();
    check(ring3_wm_depth == 0 && ring3_wm_shell_gen == 0, "5d 深さ 0 に戻る leave で印 0");
    ring3_wm_shell_gen = 5;
    ring3_wm_leave();
    check(ring3_wm_depth == 0 && ring3_wm_shell_gen == 0 && ring3_wm_depth_underflow == 1,
          "5e 対の無い leave も印 0 (深さは 0 で止まる)");

    /* (b) b2⑤ ring3_context_*: clear で印 0、restore で親の印に戻る。 */
    ring3_wm_enter();
    ring3_in_syscall = 1;
    ring3_context_save(&saved);
    check(saved.wm_shell_gen == gen && saved.wm_depth == 1, "5f save が印を控える");
    ring3_context_clear();
    check(ring3_wm_shell_gen == 0 && ring3_wm_depth == 0 && ring3_in_syscall == 0,
          "5g clear (park/STOP/fault/sys_exit の longjmp 前) で印 0");
    ring3_context_restore(&saved);
    check(ring3_wm_shell_gen == gen && ring3_wm_depth == 1 && ring3_in_syscall == 1,
          "5h restore で親の印へ戻す");
    plain_context();

    /* (c) exec_run の shell 確定: retire → publish の順 (旧 shell の lease を閉じ、
     *     新しい text で登録する)。exec_finish を経ない reload でも旧 lease は残らない。 */
    check(open_in(&id, &size) == 0 && fake_in_use() == 1, "5i 旧 shell が open");
    host_commit_calls = 0;
    host_shell_commit((u32)(uptr)__start_svc_in2, (u32)(uptr)(__stop_svc_in2 - __start_svc_in2));
    check(host_commit_calls == 1, "5j appslot_shell_commit を 1 回");
    check(fake_in_use() == 0, "5k 確定は retire してから publish (旧 lease を閉じる)");
    check(g_shell_active && g_shell_service.generation == gen + 1 &&
          g_shell_text_lo == (u32)(uptr)__start_svc_in2 &&
          g_shell_text_hi == (u32)(uptr)__stop_svc_in2,
          "5l publish は確定した text 範囲 (load_base, text_sz) で登録中になる");
    check(read_in2(id, buf, 4) == OS32_ERR_STALE, "5m 旧 id は新 shell で STALE");
    check(open_in(&id, &size) == OS32_ERR_INVAL, "5n 旧 text からの open は INVAL");

    /* (d) exec_finish の shell 分岐: 回収 (exec_reclaim_owned) の後に retire。 */
    check(open_in2(&id, &size) == 0 && fake_in_use() == 1, "5o 新 shell が open");
    host_reclaim_calls = 0;
    fake_close_shell_calls = 0;
    host_shell_finish(APP_ID_SHELL, 0);
    check(host_reclaim_calls == 1 && host_reclaim_seen_close == 0 && fake_close_shell_calls == 1,
          "5p exec_reclaim_owned の後に retire");
    check(!g_shell_active && fake_in_use() == 0, "5q shell の終了で登録を外し lease を閉じる");
    check(read_in2(id, buf, 4) == OS32_ERR_INVAL, "5r 終了後の read_at は INVAL (未登録)");
    plain_context();
}

/* ========================================================================
 *  6. T7b-b3 S2: version 2 の font_guard_run (段 A/B)。F6: open は FPU を見ない
 * ======================================================================== */
static u32 guard_lease_id;
static int guard_cb_calls;
static i32 guard_cb_read;

/* callback は shell text (svc_in) の中。guard の中で read_at を 1 回呼ぶ
 * (Face の読込み・miss の raster の形)。戻り値は ctx の値。 */
static __attribute__((noinline, section("svc_in"))) int guard_cb(void *ctx)
{
    u8 buf[4];
    guard_cb_calls++;
    guard_cb_read = read_in(guard_lease_id, buf, 4);
    return (int)(uptr)ctx;
}

static void case_guard(void)
{
    u32 id = 99, size = 0, c;
    int calls;
    report("6 b3 S2 font_guard_run (F6: x87 無しの NOSYS 撤去)\n");
    plain_context();
    g_shell_service.generation = 0;
    shell_service_retire();
    publish_in();
    fake_guard_rc = 0;
    check(open_in(&id, &size) == 0, "6a 通常文脈で open (guard の外)");
    guard_lease_id = id;

    calls = fake_guard_calls;
    guard_cb_calls = 0;
    guard_cb_read = 0;
    check(guard_in(guard_cb, (void *)(uptr)42) == 42 && fake_guard_calls == calls + 1 &&
          guard_cb_calls == 1 && guard_cb_read == 4,
          "6b 通常文脈の guard は call(ctx) の戻りを返し、中の read_at が通る");

    c = shell_service_reject_count;
    calls = fake_guard_calls;
    check(guard_out(guard_cb, 0) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT) &&
          fake_guard_calls == calls, "6c 段 A: text の外からの guard は INVAL (guard を呼ばない)");

    ring3_in_syscall = 1;
    c = shell_service_reject_count;
    calls = fake_guard_calls;
    check(guard_in(guard_cb, 0) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_GUARD) &&
          fake_guard_calls == calls, "6d 段 B: WM の外の app 文脈 (in_syscall=1) は INVAL");
    plain_context();

    /* 登録済み WM 区間 (handler / pump) は read と同じく通る。 */
    ring3_in_syscall = 1;
    host_cur = APP_ID_SHELL + 1;
    host_owner = APP_ID_SHELL + 1;
    wm_enter();
    guard_cb_calls = 0;
    guard_cb_read = 0;
    check(guard_in(guard_cb, (void *)(uptr)7) == 7 && guard_cb_calls == 1 && guard_cb_read == 4,
          "6e 段 B: WM 中で印の世代が一致すれば通る");
    ring3_wm_shell_gen = 0;
    c = shell_service_reject_count;
    check(guard_in(guard_cb, 0) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_CTX_GUARD),
          "6f 段 B: 印 0 の WM 区間は INVAL");
    ring3_wm_shell_gen = g_shell_service.generation - 1;
    check(guard_in(guard_cb, 0) == OS32_ERR_INVAL, "6g 段 B: 旧世代の印の WM 区間は INVAL");
    ring3_wm_shell_gen = g_shell_service.generation;
    wm_leave();
    plain_context();

    /* kernel の guard の戻りはそのまま返す (BUSY = 再入、他の負も同じ)。 */
    fake_guard_rc = OS32_ERR_BUSY;
    guard_cb_calls = 0;
    check(guard_in(guard_cb, 0) == OS32_ERR_BUSY && guard_cb_calls == 0,
          "6h guard の BUSY をそのまま返す");
    fake_guard_rc = OS32_ERR_INVAL;
    check(guard_in(guard_cb, 0) == OS32_ERR_INVAL && guard_cb_calls == 0,
          "6i guard の他の負もそのまま返す");
    fake_guard_rc = 0;

    /* F6: open は FPU の有無を見ない (段 A/B を通れば lease を取る)。 */
    check(close_in(id) == 0 && fake_in_use() == 0, "6j close (guard の外)");
    c = shell_service_reject_count;
    check(open_in(&id, &size) == 0 && fake_in_use() == 1 &&
          shell_service_reject_count == c, "6k 段 A/B の後の open は NOSYS を返さず lease を取る");
    check(close_in(id) == 0 && fake_in_use() == 0, "6l close (guard の外)");
    c = shell_service_reject_count;
    check(open_out(&id, &size) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT),
          "6l' 段 A: text の外の open は INVAL");

    /* reload: 旧 text からの guard は INVAL、未登録も INVAL。 */
    shell_service_retire();
    c = shell_service_reject_count;
    check(guard_in(guard_cb, 0) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_INACTIVE),
          "6m 未登録 (retire 後) の guard は INVAL");
    publish_in2();
    c = shell_service_reject_count;
    check(guard_in(guard_cb, 0) == OS32_ERR_INVAL && rejected_with(c, SHELL_SVC_REJ_TEXT),
          "6n reload 後の旧 text からの guard は INVAL");
    check(guard_in2(guard_cb, (void *)(uptr)3) == 3, "6o 新 shell の text からの guard");
    shell_service_retire();
    plain_context();
}

void _start(void)
{
    report("shell_service_host (T7b-b2 S3a + S3b)\n");
    case_publish();
    case_gate();
    case_wm();
    case_reload();
    case_wire();
    case_guard();
    if (failures) {
        report("FAILED\n");
        die(1);
    }
    report("ALL OK\n");
    die(0);
}
