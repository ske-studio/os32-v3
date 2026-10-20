/* ========================================================================
 *  shell_service.c — 常駐 shell の内部入口 (T7b-b2、設計 §3)
 *
 *  表 g_shell_service の登録 (publish / retire) と、表に載せる入口 wrapper
 *  5 本の段 A (戻り番地) / 段 B (文脈)。段 C (shell lease の registry) と
 *  段 D (VFS) は kernel/font_source.c の shell 側実体が持つ。version 2 の
 *  font_guard_run (T7b-b3 S2) は段 A/B の後に kernel/font_guard.c へ。
 *
 *  wrapper は noinline で、このファイルは -fno-optimize-sibling-calls で
 *  組む (build/kernel.mk)。戻り値は volatile に受けてから返し、尾呼出しに
 *  しない — __builtin_return_address(0) が「表を呼んだ shell の番地」で
 *  あり続けるための約束 (設計 §3-2)。
 *
 *  結線 (publish / retire / wm_mark の呼出し、frame[5]) は S3b で
 *  exec/exec.c に入る。ホスト試験は tools/tests/shell_service_host.c。
 * ======================================================================== */
#include "shell_service.h"
#include "appslot.h"        /* appslot_cur / appslot_get / APP_ID_SHELL / APP_STATE_RUNNING */
#include "exec.h"           /* ring3_wm_depth / ring3_call_from_user / OS32_ERR_* */
#include "gui.h"            /* GUI_SHELL_OWNER */
#include "fd_redirect.h"    /* res_owner_get */
#include "font_source.h"    /* font_lease_shell_* / font_lease_close_shell (段 C / D) */
#include "font_guard.h"     /* font_guard_run (version 2) */

/* exec/exec.c の実体 (int 0x80 のディスパッチ中だけ 1)。 */
extern volatile int ring3_in_syscall;

/* 段 C / 段 D の実体と署名は kernel/font_source.h (font_lease_shell_* /
 * font_lease_close_shell)。 */

ShellService g_shell_service;
volatile u32 ring3_wm_shell_gen = 0;
volatile u32 shell_service_reject_count = 0;
volatile u32 shell_service_reject_last = 0;

static u32 g_shell_text_lo, g_shell_text_hi;   /* [lo, hi) */
static int g_shell_active;

static i32 shell_service_reject(u32 reason)
{
    shell_service_reject_count++;
    shell_service_reject_last = reason;
    return OS32_ERR_INVAL;
}

/* 段 A: 戻り番地 (呼び手の申告番地は使わない) が現行 shell text の中。 */
static int shell_caller_ok(u32 ret_addr, u32 *reason)
{
    if (!g_shell_active) {
        *reason = SHELL_SVC_REJ_INACTIVE;
        return 0;
    }
    if (ret_addr < g_shell_text_lo || ret_addr >= g_shell_text_hi) {
        *reason = SHELL_SVC_REJ_TEXT;
        return 0;
    }
    return 1;
}

/* 段 B (open/close): 常駐 shell の通常文脈 — int 0x80 の外、WM の外、
 * 走っているのが CPL=0 の shell (exec/exec.c の exec_disk_write_allowed と
 * 同じイディオム)。WM op の中・遅延 open は不可 (票)。 */
static int shell_plain_context(void)
{
    AppSlot *slot;
    if (ring3_in_syscall != 0 || ring3_wm_depth != 0) return 0;
    if (appslot_cur() != APP_ID_SHELL) return 0;
    slot = appslot_get(APP_ID_SHELL);
    return slot && !slot->cpl3 && res_owner_get() == GUI_SHELL_OWNER &&
           slot->state == APP_STATE_RUNNING;
}

/* 段 B (read_at/validate): CPL=3 由来でなく、通常文脈か、登録済み shell の
 * WM 区間 (handler / pump / owner_exit — 印の世代が現 generation)。
 * WM 中は res_owner_get() が app のままだが見ない (shell lease は owner 固定)。 */
static int shell_read_context(void)
{
    if (ring3_call_from_user() != 0) return 0;
    if (shell_plain_context()) return 1;
    return ring3_wm_depth > 0 &&
           ring3_wm_shell_gen == g_shell_service.generation;
}

static __attribute__((noinline)) i32 __cdecl
shell_svc_font_source_open(const char *path, u32 *out_id, u32 *out_size)
{
    volatile i32 r;
    u32 reason = 0;
    if (!shell_caller_ok((u32)(uptr)__builtin_return_address(0), &reason))
        return shell_service_reject(reason);
    if (!shell_plain_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_OPEN);
    r = font_lease_shell_open(path, g_shell_service.generation, out_id, out_size);
    return r;
}

static __attribute__((noinline)) i32 __cdecl
shell_svc_font_source_read_at(u32 id, u32 offset, void *dst, u32 len)
{
    volatile i32 r;
    u32 reason = 0;
    if (!shell_caller_ok((u32)(uptr)__builtin_return_address(0), &reason))
        return shell_service_reject(reason);
    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_READ);
    r = font_lease_shell_read_at(id, g_shell_service.generation, offset, dst, len);
    return r;
}

static __attribute__((noinline)) i32 __cdecl
shell_svc_font_source_validate(u32 id)
{
    volatile i32 r;
    u32 reason = 0;
    if (!shell_caller_ok((u32)(uptr)__builtin_return_address(0), &reason))
        return shell_service_reject(reason);
    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_READ);
    r = font_lease_shell_validate(id, g_shell_service.generation);
    return r;
}

static __attribute__((noinline)) i32 __cdecl
shell_svc_font_source_close(u32 id)
{
    volatile i32 r;
    u32 reason = 0;
    if (!shell_caller_ok((u32)(uptr)__builtin_return_address(0), &reason))
        return shell_service_reject(reason);
    if (!shell_plain_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_OPEN);
    r = font_lease_shell_close(id, g_shell_service.generation);
    return r;
}

/* version 2: guard 区間 (kernel/font_guard.c、x87 の保存は F6 で撤去) の中で
 * call(ctx)。段 B は read_at と同じ (callback の
 * 中で read_at を呼ぶので、read_at が通る文脈でだけ通す)。 */
static __attribute__((noinline)) i32 __cdecl
shell_svc_font_guard_run(int (*call)(void *), void *ctx)
{
    volatile i32 r;
    u32 reason = 0;
    if (!shell_caller_ok((u32)(uptr)__builtin_return_address(0), &reason))
        return shell_service_reject(reason);
    if (!shell_read_context()) return shell_service_reject(SHELL_SVC_REJ_CTX_GUARD);
    r = font_guard_run(call, ctx);
    return r;
}

void shell_service_publish(u32 load_addr, u32 text_size)
{
    u32 hi = load_addr + text_size;
    g_shell_active = 0;
    g_shell_service.generation++;
    if (g_shell_service.generation == 0) g_shell_service.generation = 1;  /* 0 = 未登録 */
    if (text_size == 0 || hi < load_addr) {
        g_shell_text_lo = g_shell_text_hi = 0;
        return;
    }
    g_shell_service.magic = SHELL_SERVICE_MAGIC;
    g_shell_service.version = SHELL_SERVICE_VERSION;
    g_shell_service.font_source_open = shell_svc_font_source_open;
    g_shell_service.font_source_read_at = shell_svc_font_source_read_at;
    g_shell_service.font_source_validate = shell_svc_font_source_validate;
    g_shell_service.font_source_close = shell_svc_font_source_close;
    g_shell_service.font_guard_run = shell_svc_font_guard_run;
    g_shell_text_lo = load_addr;
    g_shell_text_hi = hi;
    g_shell_active = 1;
}

void shell_service_retire(void)
{
    font_lease_close_shell();
    g_shell_active = 0;
    g_shell_text_lo = g_shell_text_hi = 0;
}

void shell_service_wm_mark(void)
{
    if (g_shell_active) ring3_wm_shell_gen = g_shell_service.generation;
}
