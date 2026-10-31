#include "v86_call.h"
#include "font_guard.h"
#include "v86_mem.h"
#ifdef OS32_R1_FIXTURE
#include "r1_fixture.h"
#endif
#include "serial_diag.h"
#include "appmem.h"
#include "v86.h"
#include "memmap.h"
#include "exec.h"
#include "kselftest.h"
#include "kselftest_probe.h"
#include "config.h"
#include "os32x_hdr.h"
#include "appslot.h"
#include "exec_heap.h"
#include "io.h"
#include "cpu.h"      /* arch_enter_user / arch_call_on_stack (arch/$(ARCH)/arch_cpu.h) */
#include "console.h"
#include "kstring.h"
#include "vfs.h"
#include "gfx.h"
#include "gfx_hal.h"   /* gfx_bb_phys_range: CPL=3 へ USER マップする範囲 */
#include "input_service.h"
#include "ime.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "kapi_print.h"
#include "kapi_strings.h"
#include "paging.h"
#include "pgalloc.h"
#include "shlib.h"
#include "lease.h"
#include "fd_redirect.h"
#include "pipe_buffer.h"
#include "shm.h"
#include "gui.h"
#include "snd_engine.h"
#include "pcm_contract.h"   /* pcm_reclaim (票 TASK_PCM_CS4231 §2-1) */
#include "con_sink.h"
#include "kbd_inject.h"   /* K7: GUI 中の kbd 待ちを満たす注入リング */
#include "launch.h"      /* T9: 起動要求表 (GUI 中の起動を WM が仲介する) */
#include "ring3_str.h"   /* ring3_pte_writable_ok / ring3_range_overlaps (往復 10) */
#include "kapi_host.h"   /* N1: Host Services のハンドル回収 (host_owner_exit) */
#include "kapi_db.h"
#include "path.h"        /* path_get_drive / path_get_cwd (CPL=3 向けの写し) */
#include "gdt.h"
#include "tss.h"
#include "ring3_ls.h"
#include "os32_kapi_slots.h"
#include "shell_service.h" /* T7b-b2: 常駐 shell の内部入口 (publish / retire / WM の印) */
#include "font_source.h"   /* T7b-b1: app の font lease の回収 (font_lease_close_owned) */

extern void shell_print(const char *s, u8 attr);
extern void shell_print_dec(u32 val, u8 color);
extern u32 sys_mem_kb;
/* arch/x86/kernel/tss.c の TSS 実体。CPL=3 遷移で TSS.ESP0 を現在のカーネル ESP に
 * 合わせる (割り込み/int 0x80 のフレームが exec_run の frame を踏まないよう)。*/
extern struct tss_entry kernel_tss;
static KernelAPI *kapi;

/* ======================================================================== */
/*  KAPI トランポリン (v2 M2)                                                */
/*                                                                          */
/*  CPL=3 アプリは本物の KAPI 表 (カーネルコードポインタ) を読めない/呼べない */
/*  ので、全 PD 共有の USER ページ 1 枚に「本物と同一レイアウトのユーザ可視表 */
/*  + スタブ列」を置き、exec はアプリにこのページのアドレスを渡す。アプリの   */
/*  api->kprintf(...) は表のスタブを呼び、スタブが int 0x80 でカーネルに入る。 */
/*  カーネル band (.bss, PDE0 共有) に置き PTE を RO+USER にする              */
/*  (CR0.WP=0 なのでカーネルは RO でも書ける = per-launch のデータ更新可)。   */
/*  レイアウト (CONTRACTS C3, KernelAPI と同一オフセット):                    */
/*    0x00 magic / 0x04 version / 0x08+ 表[i]=STUB_BASE+i*8 (予約込みで       */
/*    KAPI_FUNC_CAPACITY 本) / KAPI_DATA_FIELDS_OFF: データフィールド (値、   */
/*    v63 から固定) / STUB_BASE: 各 8B スタブ B8<slot>CD80C3                 */
/* ======================================================================== */
/* 4KB アライン用に 2 ページ分。 */
#define EXEC_TRAMP_RAW_PAGES 2
static u8  ring3_tramp_raw[PAGE_SIZE * EXEC_TRAMP_RAW_PAGES];
static u32 ring3_tramp_page = 0;             /* 4KB 境界に揃えた実アドレス (=物理) */
static void ring3_trampoline_init(void);

/* int 0x80 引数コピー+呼び出しの ASM ヘルパ (arch/x86/kernel/ring3_entry.asm)。
 * args_src から nbytes をスタックへコピーして wrapfn を cdecl 呼び出し、
 * 戻り値 (eax) を返す。 */
extern u32 kapi_invoke(void *wrapfn, const void *args_src, u32 nbytes);

/* CPL=3 由来のフォールト/不正 slot でアプリを kill (定義は下方, v2 M1e/M2d) */
void ring3_fault_kill(void);
/* CTRL+STOP で畳む (後始末は fault と同じ、記録する種別だけが違う) */
void ring3_abort_kill(void);
/* 現在のプログラムを畳む。kind は EXEC_KIND_* (票 TASK_EXIT_STATUS §2-1)。 */
void exec_exit(int status, int kind);

void exec_init(void) {
    kapi = (KernelAPI *)KAPI_ADDR;
    /* アプリ ID の表を空にし、シェル帯 (ID 1) を走っている状態にする。
     * res_owner_set(1) もここで行われる (票 K5 の D3)。 */
    appslot_init();
    /* 起動要求表 (票 T9 D3) も空から始める。 */
    launch_init();
#include "exec_kapi_init.inc"
    /* 共有メモリ先頭アドレスを公開する。
     * MEM_SHM_BASE はカーネルの __bss_end 由来で可変のため、
     * ユーザ空間側がアドレスをハードコードしてはならない。 */
    kapi->shm_base = (u32)MEM_SHM_BASE;

    /* CPL=3 用 KAPI トランポリンページを構築 (paging_init 済みが前提) */
    ring3_trampoline_init();
}

#include "exec_tramp_init.inc"

/* ======================================================================== */
/*  コンテキストは exec/appslot.{h,c} の AppSlot 表 (K5b、票 D2/I14)         */
/*                                                                          */
/*  かつては「ネスト段のスタック」(ExecContext exec_ctx_stack[]) だったが、  */
/*  GUI アプリを 4 本同時に生かすには段では足りない — 生きているのは 4 本    */
/*  でも、走っているのは 1 本、残りは OP_WAIT の中で止まっている。           */
/*  よって **ID (1 = シェル帯 / 2〜5 = アプリ) で引く表** に置き換えた。      */
/*  空き ID を必ず小さい方から配るので、CUI の入れ子 exec_run では           */
/*  従来どおり 段 = ID になる (D3)。                                         */
/*                                                                          */
/*  シェル常駐モデル (レイアウトは 1 バイトも変わっていない):                */
/*    ID 1 (シェル): MEM_SHELL_LOAD_ADDR に常駐。CPL=0、AS は作らない        */
/*    ID 2〜5 (子) : MEM_EXEC_LOAD_ADDR、CPL=3、アプリごとの物理を使う。      */
/*                   高位の固定仮想 MEM_EXEC_LOAD_ADDR〜 へ写す (D1)。      */
/* ======================================================================== */

/* ======================================================================== */
/*  グローバル状態                                                          */
/* ======================================================================== */
/* いま走っているプログラムの段。ID ではなく **深さ** で、シェル = 1。
 * 外 (drivers/input/kbd.c, arch/x86/kernel/isr_handlers.c) は「> 0 ならプログラムが走って
 * いる」としてしか見ないので意味は変わらない。 */
volatile int exec_nest_level = 0;
volatile int exec_exit_status = EXEC_SUCCESS;

/* ------------------------------------------------------------------------ */
/*  直前の同期起動 (exec_run) の結果 — 「種別 + 値」 (票 TASK_EXIT_STATUS)    */
/*                                                                          */
/*  exec_exit_status **だけでは足りない**: exit(-2) と fault はどちらも -2 で、*/
/*  起動そのものに失敗した場合 (exec_launch の早期 return) は exec_exit を    */
/*  通らないので前回の値が残る。だから                                       */
/*    - 種別は畳んだ側が渡す (exec_exit の kind 引数)                         */
/*    - exec_run が **すべての return 点で** 書く (NONE なら rc から写す)     */
/*  の 2 つを守る。GUI 経路 (exec_start / exec_resume) の子は書かない —       */
/*  その結果を読むのは WM で、読み口 (sh.bin) はカーネルの記録を見ない。      */
/* ------------------------------------------------------------------------ */
static volatile int g_last_kind = EXEC_KIND_NONE;
static volatile int g_last_code = 0;

/* longjmp の理由。exec_start / exec_resume の復帰点が park と終了を
 * 見分けるために使う (D4)。exec_run は終了しか受け取らない。 */
#define EXEC_LJ_EXIT   1
#define EXEC_LJ_PARK   2
#define EXEC_LJ_PENDING 3
static volatile int g_pending_id, g_pending_kind;
static volatile int g_longjmp_reason = EXEC_LJ_EXIT;
static volatile int g_longjmp_id = 0;      /* park した ID (resume の戻り値) */

/* いま処理中の int 0x80 フレーム (ring3_syscall_dispatch が控える)。
 * exec_park はこれを AppSlot へ写して CPL=3 の続きを保存する (D2 の (b))。 */
static u32 *g_cur_frame = 0;

/* ======================================================================== */
/*  リング3 (CPL=3) 実行状態 (v2 M1)                                        */
/*                                                                          */
/*  M1 は単一アプリのみ (リング3 のネストは後続)。CPL=3 で走るアプリの      */
/*  アドレス空間を 1 つだけ保持する。int 0x80 (sys_exit) の C 側ディスパッチ */
/*  がここを参照して master PD へ戻し AS を破棄する。                        */
/* ======================================================================== */

/* T2c virtual stack. The caller slot carries its actual size. */
#define RING3_USTACK_TOP MEM_APP_STACK_TOP
#define RING3_HEAP_TOP (RING3_STACK_BOTTOM - PAGE_SIZE)
#define RING3_STACK_BOTTOM (g_cur_app ? g_cur_app->stack_base : MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE)
/* いま走っている CPL=3 アプリのスロット (0 = 居ない)。かつての
 * g_ring3_as / g_ring3_active を 1 本にまとめたもの。park すると 0 になり、
 * resume で戻る。生きているだけで走っていないアプリは表に横たわっている。 */
static AppSlot *g_cur_app = 0;

/* CPL=3 アプリをフォールト (#PF/#GP) で kill した回数 (CONTRACTS C6, v2 M1e)。
 * static にせずカーネルシンボルとして公開する (kselftest_pass 等と同じ形)。
 * PM の V4 検証が emu_read_mem で読む。 */
volatile u32 fault_kill_count = 0;

/* 直前の**入れ子 0 段の**起動 (常駐シェル / gshell) が「KAPI データ欄の配置
 * 違い」で断られたか (票 TASK_KAPI_DATA_FIELDS)。kernel.c のシェル起動ループが、
 * 常駐シェルを断ったときの案内に使う。**入れ子 1 段以上 (シェルから起動した
 * アプリ) の拒否では立てない** — 立てると、シェルが走っている間に古いアプリを
 * 1 本断っただけで、後でシェルが負の値で戻ったときに「shell.bin を作り直せ」
 * と誤って止まる (実装レビュー R1、Opus N2)。 */
static int g_layout_reject = 0;

int exec_layout_rejected(void)
{
    return g_layout_reject;
}

/* ring3 syscall (wrap) 実行中フラグ (v2 M2e フォールトガードの核)。
 * dispatcher が kapi_invoke を挟む間だけ立てる。この間に #PF/#GP が起きたら
 * (wrap 内 = CPL=0 でも) カーネル停止でなくアプリだけ kill する。可変長 %s の
 * ような静的に検証できないポインタ deref もこれで捕捉できる。
 * 非 static (isr_handlers.c が extern で参照)。 */
volatile int ring3_in_syscall = 0;

/* カーネルが WM (gshell、CPL=0 の常駐シェル) のコードへ入っている深さ
 * (2026-09-26、filer が窓も出さずに消えた件)。gui_call のハンドラ (X1 / X3)、
 * syscall 境界のポンプ (X4)、exec_exit の owner_exit の入口で +1、出口で -1。
 * WM は**アプリの syscall の中で**走る (契約 T8) ので、この間も
 * ring3_in_syscall は 1 のまま。ring3_user_range_ok / ring3_user_ranges_writable
 * / tramp_copy の 3 つの門は `ring3_guard_active(ring3_in_syscall,
 * ring3_wm_depth)` (exec/ring3_str.c) で「アプリ由来か」を決め、深さが 1 以上
 * なら WM 自身のポインタ (自分のスタックの MouseInfo 等) を素通しする。
 * #PF/#GP の帰属 (arch/x86/kernel/isr_handlers.c) は ring3_in_syscall だけを見るので
 * **変わらない**。
 * WM を longjmp で抜ける経路 (exec_park* / ring3_kill_kind / sys_exit) は
 * 出口を通らないので、そこと **ディスパッチャの入口** で 0 に戻す —
 * 「CPL=3 の syscall は必ず深さ 0 から始まる」が不変条件。
 * 非 static (kernel/gui.c が extern で参照、kselftest が読む)。 */
volatile int ring3_wm_depth = 0;

void ring3_wm_enter(void)
{
    ring3_wm_depth++;
    /* T7b-b2 §3-3: 一番外側の WM 区間に入ったとき、登録中の shell の世代を
     * 印にする (shell の font read はこの印が現世代の間だけ WM 中でも通る)。 */
    if (ring3_wm_depth == 1) shell_service_wm_mark();
}

volatile u32 ring3_wm_depth_underflow = 0;

/* WM (gshell) のコードへ入っている間 (ring3_wm_depth > 0) にアプリを
 * フォールトで畳んだ数 (2026-09-26、代行レビュー P3)。WM の中の #PF/#GP も
 * KAPI の門の拒否も、帰属はアプリ (ring3_in_syscall) なので**アプリの kill と
 * して畳まれ**、fault_kill_count が +1 するだけで例外画面は出ない (§4-61 の
 * 「例外 0 件・fault_kill_count だけ増える」)。これが増えていれば落ちた場所は
 * WM の文脈。数えるのは ring3_kill_kind の深さを 0 に戻す**前** (後に置くと
 * 常に 0)。CTRL+STOP (ABORTED) は数えない。挙動は変えない。
 * fault_kill_count と同じくカーネルシンボルを emu_read_mem で読む。 */
volatile u32 ring3_wm_fault_count = 0;

void ring3_wm_leave(void)
{
    if (ring3_wm_depth > 0) {
        ring3_wm_depth--;
    } else {
        /* 対になっていない出口。深さは 0 で止める (負にすると門が安全側に
         * 倒れて WM のポインタでアプリが kill される) が、黙って止めずに
         * 数える (exec.h の注記)。 */
        ring3_wm_depth_underflow++;
    }
    /* T7b-b2 §3-3: WM 区間を出きったら印を消す。 */
    if (ring3_wm_depth == 0) ring3_wm_shell_gen = 0;
}

/* A launch/resume stack survives the child's longjmp; dispatch stacks do not.
 * Capture before setjmp and never modify the snapshot afterwards. Volatile
 * prevents stack-slot reuse: our assembly setjmp has no returns_twice attribute. */
typedef struct {
    CallerAccessFrame caller;
    u32 *frame;
    u32 esp0;
    int in_syscall, wm_depth;
    u32 wm_shell_gen;   /* T7b-b2: WM 区間の世代付きの印 (ring3_wm_shell_gen) */
} Ring3CallContext;

static void ring3_context_save(volatile Ring3CallContext *out) __attribute__((noinline));
static void ring3_context_clear(void) __attribute__((noinline));
static void ring3_context_restore(const volatile Ring3CallContext *saved) __attribute__((noinline));

static void ring3_context_save(volatile Ring3CallContext *out)
{
    caller_access_save(&out->caller);
    out->frame = g_cur_frame;
    out->esp0 = tss_get_esp0();
    out->in_syscall = ring3_in_syscall;
    out->wm_depth = ring3_wm_depth;
    out->wm_shell_gen = ring3_wm_shell_gen;
}

static void ring3_context_clear(void)
{
    caller_access_invalidate();
    g_cur_frame = 0;
    ring3_in_syscall = 0;
    ring3_wm_depth = 0;
    ring3_wm_shell_gen = 0;
}

static void ring3_context_restore(const volatile Ring3CallContext *saved)
{
    /* Cleanup may enable IRQs. Restore the parent's entry stack together with
     * its syscall context before returning to CPL3 (or the resident shell). */
    unsigned int flags = irq_save();
    caller_access_leave(&saved->caller);
    g_cur_frame = saved->frame;
    tss_set_esp0(saved->esp0);
    ring3_in_syscall = saved->in_syscall;
    ring3_wm_depth = saved->wm_depth;
    ring3_wm_shell_gen = saved->wm_shell_gen;
    irq_restore(flags);
}

volatile u32 ring3_caller_reject_count = 0;

int ring3_call_from_user(void)
{
    return ring3_guard_active(ring3_in_syscall, ring3_wm_depth);
}

/* Match the loaded image after the same cwd/path normalization as VFS. */
static int exec_disk_write_path_allowed(const char *resolved)
{
    static const char *const paths[] = SYS_DISK_WRITE_PATHS;
    char absolute[VFS_MAX_PATH];
    u32 i;
    if (vfs_resolve_path(resolved, absolute, sizeof(absolute)) != VFS_OK) return 0;
    for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++)
        if (kstrcmp(absolute, paths[i]) == 0) return 1;
    return 0;
}

int exec_disk_write_allowed(void)
{
    struct caller_access caller;
    AppSlot *slot;
    /* No WM elevation: this capability belongs to a CUI foreground caller. */
    if (ring3_wm_depth || con_sink_is_enabled()) return 0;
    slot = appslot_get(appslot_cur());
    if (!slot || slot->state != APP_STATE_RUNNING || slot->gui) return 0;
    /* The resident CPL0 shell calls wrappers directly, outside int80. */
    if (!ring3_in_syscall)
        return appslot_cur() == APP_ID_SHELL && !slot->cpl3 &&
               res_owner_get() == APP_ID_SHELL;
    /* Invalid/stale USER frames must never fall back to trusted access. */
    if (!caller_access_get_user(&caller)) return 0;
    return slot->cpl3 && slot->disk_write_authorized;
}

/* Public TVRAM reads belong only to the CUI foreground caller. Internal
 * console/IME/selftest readers deliberately bypass this wrapper policy. */
int exec_tvram_read_allowed(void)
{
    struct caller_access caller;
    AppSlot *slot;
    if (ring3_wm_depth || con_sink_is_enabled()) return 0;
    slot = appslot_get(appslot_cur());
    if (!slot || slot->state != APP_STATE_RUNNING || slot->gui) return 0;
    if (!ring3_in_syscall)
        return appslot_cur() == APP_ID_SHELL && !slot->cpl3 &&
               res_owner_get() == APP_ID_SHELL;
    return slot->cpl3 && caller_access_get_user(&caller);
}

#include "exec_tramp_strings.inc"

/* exec.c では呼ばないが、tools/gen_kdebug_imports.py がこの宣言から型を読む (消すと生成が止まる)。 */
extern i32 __cdecl kapi_reserved_nosys(void);

/* kdebug の exec 自己試験へ写す口 (include/kselftest_probe.h)。 */
void kdi_selftest_exec_view(struct kselftest_exec_view *out)
{
    out->tramp_page = ring3_tramp_page;
    out->api = kapi;
}



#include "exec_abort.inc"

/* ユーザポインタ引数の早期範囲検証 (v2 M2e 補助)。exec が CPL=3 アプリに
 * USER マップした領域 (共有ライブラリ帯/プログラム帯/ユーザスタック/SHM) を許可。
 * 低位 (TVRAM〜BRG・フォント) は e11b1 で例外を外した — 画面は lease 経由だけ。NULL は通す。長さ付き引数は生成 wrap が NULL と全域を検査する。
 * 早期検査に残る文字列・opaque・関数ポインタの NULL の意味は wrap/target が決める。
 * 範囲外 (例: 0xDEADBEEF) は wrap に入る前に弾き、カーネル状態不整合を避ける。
 * 可変長引数はここでは見えないのでフォールトガードが担保する。 */
int ring3_ptr_ok(u32 p)
{
    if (p == 0) return 1;                         /* NULL の意味は wrap/target */
    if (exec_tramp_page_addr() && p >= exec_tramp_page_addr() + RING3_USTR_OFF &&
        p - (exec_tramp_page_addr() + RING3_USTR_OFF) < RING3_USTR_CAP) return 1;
    if (p >= MEM_SHLIB_BASE && p < RING3_HEAP_TOP) return 1;
        /* 共有ライブラリ帯 (K3: .rodata の文字列や .data の構造体を KAPI に
         * 渡せる) + アプリの code/data/bss/heap (ガード直下まで)。
         * **ここは「番地として正しいか」だけを見る。** かつて
         * 「.text への書き込みは PTE が RO なので #PF で捕まる」と書いて
         * あったが、それは誤り: OS32 は **CR0.WP = 0** で走るので CPL=0 の
         * カーネル (= KAPI の wrapper) は RO の USER ページにも書ける。
         * 出力引数が本当に書ける番地かは、wrapper 先頭の
         * `ring3_user_ranges_writable` が見る (票 TASK_KAPI_OUTPUT_GUARD)。 */
    if (p >= RING3_STACK_BOTTOM && p < RING3_USTACK_TOP) return 1;
        /* ユーザスタック帯。ガードページ [RING3_GUARD_BASE, RING3_STACK_BOTTOM)
         * は不許可 (ここを指すポインタは早期検証で kill)。 */
    if (p >= (u32)MEM_SHM_BASE &&
        p <  (u32)MEM_SHM_BASE + (u32)MEM_SHM_SIZE) return 1;  /* SHM */
    if (p >= MEM_LEASE_BASE && p < MEM_LEASE_END) {
        struct caller_access c;
        u32 pa;
        unsigned int saved = irq_save();
        int ok = caller_access_get_user(&c) && caller_access_page(&c, p, 0, &pa);
        irq_restore(saved);
        return ok;
    }
    return 0;
}

/* B1 guards walk the saved caller's managed tables without a CR3 switch.
 * Diagnostic symbols remain available to PM guest probes. */
volatile u32 ring3_range_reject_count = 0;
volatile u32 ring3_range_reject_last = 0;
volatile u32 ring3_range_reject_addr = 0;
volatile u32 ring3_range_reject_page = 0;
volatile u32 ring3_range_reject_heap_top = 0;

static int ring3_range_refuse(u32 why, u32 p, u32 page)
{
    ring3_range_reject_count++;
    ring3_range_reject_last = why;
    ring3_range_reject_addr = p;
    ring3_range_reject_page = page;
    ring3_range_reject_heap_top = (u32)RING3_HEAP_TOP;
    return 0;
}

int ring3_user_ranges_writable(u32 va, u32 la, u32 vb, u32 lb)
{
    /* **CPL=0 の直呼びは対象外** (常駐シェル / gshell はローカル変数を渡す)。
     * 判定は「呼び出し経路」で決める — CR3 が master かどうかで代用しない
     * (Approve 後の注意 2)。既存の ring3_user_range_ok と同じ門。
     * **WM の文脈 (ring3_wm_depth > 0) も常駐側の直呼び扱い** — WM はアプリの
     * syscall の中で走るので ring3_in_syscall だけでは区別できない
     * (2026-09-26、ring3_wm_depth の注記)。
     * これは**いま渡されたポインタ**の門。アプリが**前に登録した**ポインタは
     * 登録者 PD を使う redir_access で扱う (T2d d0b)。 */
    if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return 1;
    return ring3_user_ranges_writable_always(va, la, vb, lb);
}

int ring3_user_ranges_writable_always(u32 va, u32 la, u32 vb, u32 lb)
{
    struct caller_access c;
    if (!la && !lb) return 1;
    if (!caller_access_get_user(&c))
        return ring3_range_refuse(RING3_RANGE_WR_TABLE, la ? va : vb, 0);
    if (!check_caller_write_range(&c, (void *)(uptr)va, la))
        return ring3_range_refuse(RING3_RANGE_WR_TABLE, va, 0);
    if (!check_caller_write_range(&c, (void *)(uptr)vb, lb))
        return ring3_range_refuse(RING3_RANGE_WR_TABLE, vb, 0);
    return 1;
}

int ring3_user_range_writable(u32 p, u32 len)
{
    return ring3_user_ranges_writable(p, len, 0, 0);
}

i32 exec_trim_done(u32 epoch)
{
    if (v86_call_pinned()) return OS32_ERR_BUSY;
    struct caller_access c;
    AppSlot *slot;
    u32 pages;
    if (!caller_access_get_user(&c) || kctx_irq_depth || kctx_exc_depth || ring3_wm_depth)
        return OS32_ERR_INVAL;
    slot = appslot_get(c.app_id);
    if (!slot || slot->as != c.as || !slot->gui || !slot->cpl3)
        return OS32_ERR_INVAL;
    if (!appslot_trim_done(c.app_id, epoch)) return OS32_ERR_STALE;
    pages = exec_heap_user_trim(slot->as);
    appslot_trim_pages_total += pages;
    return (i32)pages;
}

int ring3_user_range_ok(u32 p, u32 len)
{
    struct caller_access c;
    unsigned int saved;
    int ok;
    if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return 1;
    if (!p) return ring3_range_refuse(RING3_RANGE_NULL, p, 0);
    if (!len) return 1;
    if (len - 1 > ~(u32)0 - p)
        return ring3_range_refuse(RING3_RANGE_OVERFLOW, p, 0);
    ok = caller_access_get_user(&c);
    for (u32 va = p, left = len; ok && left;) {
        u32 pa, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        saved = irq_save();
        ok = caller_access_page(&c, va, 0, &pa);
        irq_restore(saved);
        if (n > left) n = left;
        left -= n;
        if (left) va += n;
    }
    if (!ok) return ring3_range_refuse(RING3_RANGE_BAND, p, 0);
    return 1;
}

#include "ksetjmp.h"

#include "exec_image_map.inc"

#include "exec_finish.inc"

#include "exec_syscall.inc"
#include "exec_stdin.inc"

#include "exec_fault.inc"

/* ======================================================================== */
/*  ring3_resume — 保存した CPL=3 フレームへ戻る (P4、arch/x86/kernel/ring3_entry.asm)*/
/*  cli → TSS.ESP0 → CR3 → フレームを積んで popad; iretd を割り込み禁止で    */
/*  一続きに行う。戻らない。                                                 */
/* ======================================================================== */
extern void ring3_resume(const u32 *frame, u32 pd_phys, void *tss);

/* 常駐シェル帯の分割 (画像+sbrk / exec_heap)。文脈の公開と読み込みより前に
 * 1 回だけ決める。短い exec_heap の残りは予約のままで、sbrk には貸さない。 */
static int exec_shell_layout(u32 text, u32 bss, u32 heap,
                             u32 *sbrk_limit, u32 *exec_base, u32 *exec_size)
{
    if (text > MEM_SHELL_MAX_SIZE || bss > MEM_SHELL_MAX_SIZE - text)
        return EXEC_ERR_NOMEM;
    u32 image_end = (MEM_SHELL_LOAD_ADDR + text + bss + MEM_SHELL_HEAP_ALIGN - 1)
                   & ~(MEM_SHELL_HEAP_ALIGN - 1);
    u32 exec_end = MEM_SHELL_STACK_TOP - MEM_SHELL_STACK_SIZE - PAGE_SIZE;
    u32 base = exec_end - MEM_SHELL_EXEC_HEAP_SIZE;
    if (image_end > base || base - image_end < MEM_SHELL_IMAGE_HEADROOM ||
        heap > MEM_SHELL_EXEC_HEAP_SIZE)
        return EXEC_ERR_NOMEM;
    *sbrk_limit = base;
    *exec_base = base;
    *exec_size = heap ? heap : MEM_SHELL_EXEC_HEAP_SIZE;
    return 0;
}

#include "exec_image.inc"
#include "exec_argv.inc"

#include "exec_launch.inc"

/* ======================================================================== */
/*  exec_run — 従来どおり「子が終わるまで塞ぐ」起動 (CUI の入れ子はこれ)     */
/* ======================================================================== */
/* 起動しなかったとき (exec_exit を通っていないとき) の種別。値から作れる
 * のはここだけ — exec_launch の戻り値は EXEC_ERR_* / OS32_ERR_* で、
 * 「子の終了コード」と混ざらない (子が終わっていれば exec_exit が先に
 * 書いているので、この写像は走らない)。
 * appslot_start_admit の OS32_ERR_FULL / OS32_ERR_INVAL も「起こせなかった」
 * = 次の候補へ進まない側 (NOMEM 相当) に寄せる (票 §2-1)。 */
static int exec_map_launch_err(int rc)
{
    switch (rc) {
    case EXEC_ERR_NOT_FOUND: return EXEC_KIND_NOT_FOUND;
    case EXEC_ERR_INVALID:   return EXEC_KIND_INVALID;
    case EXEC_ERR_NOMEM:     return EXEC_KIND_NOMEM;
    case OS32_ERR_FULL:      return EXEC_KIND_NOMEM;
    case OS32_ERR_INVAL:     return EXEC_KIND_NOMEM;
    default:                 return EXEC_KIND_GENERAL;
    }
}

int exec_run(const char *cmdline)
{
    int rc;

    /* 票 §2-1: **入る前に消す**。消さないと「起動しなかった」経路
     * (exec_launch の早期 return 13 か所) で前回の記録が残り、
     * 成功の直後に未知のコマンドを打つと前の子の終了コードが返る。 */
    g_last_kind = EXEC_KIND_NONE;
    g_last_code = 0;

    rc = exec_launch(cmdline, 0);

    if (g_last_kind == EXEC_KIND_NONE) {
        g_last_kind = exec_map_launch_err(rc);
        g_last_code = 0;
    }
    return rc;
}

/* ======================================================================== */
/*  exec_last_result — 直前の exec_run の結果 (KAPI v55、決裁 E1)             */
/*                                                                          */
/*  戻り値: 0 = 記録あり / OS32_ERR_INVAL = 記録なし (このとき *kind は       */
/*  EXEC_KIND_NONE、*code は 0 に揃える — 呼び手が前の値を読み続けない)。     */
/* ======================================================================== */
int exec_last_result(int *kind, int *code)
{
    int k = g_last_kind;
    int c = g_last_code;

    if (k == EXEC_KIND_NONE) {
        if (kind) *kind = EXEC_KIND_NONE;
        if (code) *code = 0;
        return OS32_ERR_INVAL;
    }
    if (kind) *kind = k;
    if (code) *code = c;
    return 0;
}

/* ======================================================================== */
/*  exec_start — 塞がない起動 (KAPI v44、D4 / 決裁 D9-5)                     */
/*                                                                          */
/*  戻り値: >0 = app_id (2〜5)。最初の OP_WAIT まで進んで park した          */
/*          0  = park より前に終了した (回収済み、gui_owner_exit 配送済み)   */
/*          <0 = 起動しなかった (OS32_ERR_INVAL / OS32_ERR_FULL /            */
/*               EXEC_ERR_NOMEM / EXEC_ERR_NOT_FOUND / EXEC_ERR_INVALID)     */
/*  owner 1 (シェル帯) からのみ — 判定は gui_register と同じ形 (契約 S2)。    */
/* ======================================================================== */
i32 exec_start(const char *cmdline)
{
    if (res_owner_get() != APP_ID_SHELL) return OS32_ERR_INVAL;
    if (cmdline == 0 || cmdline[0] == '\0') return OS32_ERR_INVAL;
    return exec_launch(cmdline, 1);
}

#include "exec_schedule.inc"
