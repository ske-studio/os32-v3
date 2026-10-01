#include "memmap.h"
#include "exec.h"
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
#include "kbd.h"
#include "kmalloc.h"
#include "kprintf.h"
#include "paging.h"
#include "pgalloc.h"
#include "shlib.h"
#include "lease.h"
#include "fd_redirect.h"
#include "pipe_buffer.h"
#include "shm.h"
#include "gui.h"
#include "snd_engine.h"
#include "pcm_cs4231.h"   /* pcm_reclaim (票 TASK_PCM_CS4231 §2-1) */
#include "con_sink.h"
#include "kbd_inject.h"   /* K7: GUI 中の kbd 待ちを満たす注入リング */
#include "launch.h"      /* T9: 起動要求表 (GUI 中の起動を WM が仲介する) */
#include "ring3_str.h"   /* ring3_pte_writable_ok / ring3_range_overlaps (往復 10) */
#include "kapi_host.h"   /* N1: Host Services のハンドル回収 (host_owner_exit) */
#include "ring3_str.h"   /* T9 §12 R1: KAPI が CPL=3 へ返す文字列の置き場 */
#include "kapi_db.h"
#include "path.h"        /* path_get_drive / path_get_cwd (CPL=3 向けの写し) */
#include "gdt.h"
#include "tss.h"

extern void shell_print(const char *s, u8 attr);
extern void shell_print_dec(u32 val, u8 color);
extern u32 sys_mem_kb;
/* kernel/tss.c の TSS 実体。CPL=3 遷移で TSS.ESP0 を現在のカーネル ESP に
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
static u8  ring3_tramp_raw[PAGE_SIZE * 2];   /* 4KB アライン用に 2 ページ分 */
static u32 ring3_tramp_page = 0;             /* 4KB 境界に揃えた実アドレス (=物理) */
static void ring3_trampoline_init(void);

/* int 0x80 引数コピー+呼び出しの ASM ヘルパ (kernel/ring3_entry.asm)。
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

/* 写し場 (票 T9 §12 R1) はスタブの後ろに置く。KAPI が増えて 1 ページに
 * 収まらなくなったら **ここでビルドが落ちる** — 実機では「cd の直後に
 * pwd が化ける」としか見えないので、静的に止める。 */
STATIC_ASSERT(MEM_PHYS_RAM_CEILING <= MEM_APP_BAND_BASE, ram_below_app_band);
STATIC_ASSERT((MEM_EXEC_HEAP_BASE >> 22) >= (MEM_APP_BAND_BASE >> 22) &&
              (MEM_EXEC_HEAP_BASE >> 22) < (MEM_APP_BAND_MAX_TOP >> 22), heap_pde_in_app_band);
STATIC_ASSERT(((MEM_APP_STACK_TOP - 1) >> 22) >= (MEM_APP_BAND_BASE >> 22) &&
              ((MEM_APP_STACK_TOP - 1) >> 22) < (MEM_APP_BAND_MAX_TOP >> 22), stack_pde_in_app_band);
STATIC_ASSERT(RING3_USTR_OFF + RING3_USTR_CAP <= (u32)PAGE_SIZE,
              ring3_ustr_fits_in_trampoline_page);

/* データ欄の固定配置 (票 TASK_KAPI_DATA_FIELDS、KAPI v63)。生成器
 * (sdk/gen_kapi.py) の KAPI_DATA_FIELDS_OFF と構造体の実際の並びが一致し、
 * 1 ページの容量 (表 + スタブ 8B × 容量 + 写し場) を越えないことを静的に見る。
 * ここが落ちたら関数表の容量 (kapi.json の func_capacity) を見直す票を起こす。 */
STATIC_ASSERT(__builtin_offsetof(KernelAPI, sbrk_heap_limit) ==
              (u32)KAPI_DATA_FIELDS_OFF, kapi_data_fields_fixed);
STATIC_ASSERT(__builtin_offsetof(KernelAPI, shm_base) ==
              (u32)KAPI_DATA_FIELDS_OFF + 4u, kapi_shm_base_fixed);
STATIC_ASSERT((u32)KAPI_DATA_IDX_SBRK_HEAP_LIMIT * 4u == (u32)KAPI_DATA_FIELDS_OFF,
              kapi_data_idx_sbrk);
STATIC_ASSERT((u32)KAPI_DATA_IDX_SHM_BASE * 4u == (u32)KAPI_DATA_FIELDS_OFF + 4u,
              kapi_data_idx_shm);
STATIC_ASSERT((u32)KAPI_FUNC_COUNT <= (u32)KAPI_FUNC_CAPACITY, kapi_func_capacity);
STATIC_ASSERT(sizeof(KernelAPI) + (u32)KAPI_FUNC_CAPACITY * 8u + RING3_USTR_CAP
              <= (u32)PAGE_SIZE, kapi_trampoline_one_page);
/* 本物の表は KAPI_ADDR からの 4KB (MEM_KAPI_SIZE) に置く (include/memmap.h)。 */
STATIC_ASSERT(sizeof(KernelAPI) <= (u32)MEM_KAPI_SIZE, kapi_table_fits_reserve);

/* ======================================================================== */
/*  ring3_trampoline_init — トランポリンページの構築 (v2 M2b)               */
/* ======================================================================== */
static void ring3_trampoline_init(void)
{
    u32 page = ((u32)ring3_tramp_raw + PAGE_SIZE - 1) & ~(u32)(PAGE_SIZE - 1);
    u32 *tbl = (u32 *)page;
    u32 stub_base = page + RING3_USTR_STUB_OFF;   /* 全 struct の後ろ */
    u32 i;

    ring3_tramp_page = page;

    /* magic / version は本物と同じ値 */
    tbl[0] = kapi->magic;
    tbl[1] = kapi->version;

    /* 予約スロット (KAPI_FUNC_COUNT..KAPI_FUNC_CAPACITY-1) にもスタブを置く
     * (票 TASK_KAPI_DATA_FIELDS)。NULL にすると旧 SDK が新しい関数を呼んだ
     * とき 0 番地へ飛ぶ。スタブならディスパッチャが slot >= KAPI_FUNC_COUNT
     * でアプリだけ kill する。 */
    for (i = 0; i < KAPI_FUNC_CAPACITY; i++) {
        u8 *st = (u8 *)(stub_base + i * 8u);
        /* ユーザ可視表: entry[i] = スタブ i の番地 (KernelAPI fn[i] と同一 offset) */
        tbl[2 + i] = stub_base + i * 8u;
        /* スタブ: B8 <slot:imm32> CD 80 C3  (mov eax,slot; int 0x80; ret) */
        st[0] = 0xB8;
        st[1] = (u8)(i & 0xFF);
        st[2] = (u8)((i >> 8) & 0xFF);
        st[3] = (u8)((i >> 16) & 0xFF);
        st[4] = (u8)((i >> 24) & 0xFF);
        st[5] = 0xCD;   /* int */
        st[6] = 0x80;   /* 0x80 */
        st[7] = 0xC3;   /* ret */
    }

    /* データフィールド (値): KernelAPI 表と同一オフセット。v63 から
     * KAPI_DATA_FIELDS_OFF に固定 (関数の数ではなく容量の後ろ)。
     * sbrk_heap_limit は exec_run が launch 時に上書きする。 */
    tbl[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = 0;
    tbl[KAPI_DATA_IDX_SHM_BASE] = (u32)MEM_SHM_BASE;

    /* 全 PD 共有で RO+USER マップ (kernel band PDE0)。i386 は NX なしなので
     * RO でも実行可能 (スタブ実行 OK)。ユーザは書けない = スタブ改竄不可。
     * CR0.WP=0 によりカーネルは RO でも書ける (per-launch のデータ更新)。 */
    paging_set_page(page, page, PAGE_RO | PTE_USER);
}

/* スタックを4バイト境界に揃えるためのマスク */
#define STACK_ALIGN_MASK 3

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
/*    ID 1 (シェル): 0x300000 に常駐。CPL=0、AS は作らない                    */
/*    ID 2〜5 (子) : MEM_EXEC_LOAD_ADDR、CPL=3、アプリごとの物理を使う。      */
/*                   高位の固定仮想 MEM_EXEC_LOAD_ADDR〜 へ写す (D1)。      */
/* ======================================================================== */

/* ======================================================================== */
/*  グローバル状態                                                          */
/* ======================================================================== */
/* いま走っているプログラムの段。ID ではなく **深さ** で、シェル = 1。
 * 外 (drivers/kbd.c, kernel/isr_handlers.c) は「> 0 ならプログラムが走って
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
#define RING3_USTACK_SIZE MEM_EXEC_STACK_SIZE
#define RING3_HEAP_TOP (RING3_STACK_BOTTOM - PAGE_SIZE)
#define RING3_STACK_BOTTOM (g_cur_app ? g_cur_app->stack_base : MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE)
static u32 g_ring3_band_top = MEM_APP_STACK_TOP;
static u32 g_ring3_band_pdes = MEM_APP_BAND_MAX_PDES;
static void ring3_band_set(u32 pdes)
{
    (void)pdes;
    g_ring3_band_top = MEM_APP_STACK_TOP;
    g_ring3_band_pdes = MEM_APP_BAND_MAX_PDES;
}

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

/* sbrk 物理の二段構え (決裁 2026-09-11) の観測点。KAPI にはしない —
 * fault_kill_count と同じくカーネルシンボルを emu_read_mem で読む。
 *   exec_sbrk_tier_last  : 直近の CPL=3 起動が採った段 (1 = 従来式 / 2 = 最低分)
 *   exec_sbrk_tier_count : 段ごとの累計 ([0] = 段 1、[1] = 段 2)
 * 数えるのは 3 領域を実際に張り終えた起動だけ (途中で失敗したものは数えない)。*/
volatile u32 exec_sbrk_tier_last = 0;
volatile u32 exec_sbrk_tier_count[2] = { 0, 0 };

/* ring3 syscall (wrap) 実行中フラグ (v2 M2e フォールトガードの核)。
 * dispatcher が kapi_invoke を挟む間だけ立てる。この間に #PF/#GP が起きたら
 * (wrap 内 = CPL=0 でも) カーネル停止でなくアプリだけ kill する。可変長 %s の
 * ような静的に検証できないポインタ deref もこれで捕捉でき [ABI4] を塞ぐ。
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
 * #PF/#GP の帰属 (kernel/isr_handlers.c) は ring3_in_syscall だけを見るので
 * **変わらない**。
 * WM を longjmp で抜ける経路 (exec_park* / ring3_kill_kind / sys_exit) は
 * 出口を通らないので、そこと **ディスパッチャの入口** で 0 に戻す —
 * 「CPL=3 の syscall は必ず深さ 0 から始まる」が不変条件。
 * 非 static (kernel/gui.c が extern で参照、kselftest が読む)。 */
volatile int ring3_wm_depth = 0;

void ring3_wm_enter(void)
{
    ring3_wm_depth++;
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
}

int ring3_call_from_user(void)
{
    return ring3_guard_active(ring3_in_syscall, ring3_wm_depth);
}

/* ======================================================================== */
/*  vfs_cwd_user — sys_getcwd の実体 (票 T9 §12 R1、KAPI の追加はしない)     */
/*                                                                          */
/*  `sdk/kapi.json` の `sys_getcwd` の target を `vfs_cwd` からこれに差し    */
/*  替えてある。スロット番号も引数も戻り型も変わらないので [ABI2] の範囲内で、*/
/*  KAPI の版も上げない (外から見える約束が 1 つも動かないため — 版を上げると */
/*  既存バイナリの min_api_ver が一斉に足りなくなる副作用の方が大きい)。     */
/*                                                                          */
/*  CPL=3 の呼び手には **トランポリンページ内の写し** を返す。カーネル帯の    */
/*  static `cwd` は USER ビットが無く、読んだ瞬間に #PF → fault kill になる  */
/*  (sh.bin の `cd` / `pwd`、apps/edit がこれを踏んでいた)。                 */
/*  写しは呼ばれるたびに上書きする — 呼び手は次の KAPI 呼び出しより前に      */
/*  読み切ること (docs/KAPI_SPEC.md の sys_getcwd の行に注記)。              */
/* ======================================================================== */
/* CPL=3 の呼び手 (ring3_in_syscall) にはトランポリンページの写しを、CPL=0 には
 * src をそのまま返す (vfs_cwd_user / vfs_devname_user / path_get_*_user 共通)。 */
static const char *tramp_copy(const char *src)
{
    char *scratch = 0;
    if (ring3_tramp_page != 0) {
        scratch = (char *)(ring3_tramp_page + RING3_USTR_OFF);
    }
    return ring3_user_str(ring3_guard_active(ring3_in_syscall, ring3_wm_depth),
                          scratch, RING3_USTR_CAP, src);
}

const char *vfs_cwd_user(void) { return tramp_copy(vfs_cwd()); }

/* ======================================================================== */
/*  vfs_devname_user — vfs_devname の実体 (票 TASK_HDD_INSTALL 段 2、        */
/*  KAPI の追加はしない)                                                     */
/*                                                                          */
/*  sys_getcwd と同じ理由・同じ手: fs/vfs.c の vfs_devname はマウント表      */
/*  (カーネル帯の static、USER ビット無し) の dev_name をそのまま返すので、   */
/*  CPL=3 の呼び手が読むと #PF → fault kill になる。2026-09-24 の NP21/W で   */
/*  cdinst (CPL=3) が hd0 の検査の `vfs_devname("/")` で落ちた (sh.bin の     */
/*  hdprep と filer も同じ経路)。`sdk/kapi.json` の target を差し替え、CPL=3 */
/*  にはトランポリンページ内の写し (sys_getcwd と共用の 1 本、次の KAPI 呼び */
/*  出しより前に読み切ること) を返す。スロット・引数・戻り型は不変で、版も  */
/*  上げない。CPL=0 の呼び手 (常駐シェル) には従来どおりの番地が返る。       */
/* ======================================================================== */
const char *vfs_devname_user(const char *prefix) { return tramp_copy(vfs_devname(prefix)); }

/* ======================================================================== */
/*  path_get_drive_user / path_get_cwd_user — lib/path.c の 2 本の実体      */
/*  (票 TASK_HDD_INSTALL 段 2 のレビュー往復 2、KAPI の追加はしない)         */
/*                                                                          */
/*  lib/path.c の cur_drive / cur_cwd もカーネル帯の static で、そのまま      */
/*  返すと CPL=3 の呼び手は読んだ瞬間に fault kill になる (vfs_devname と     */
/*  同じ種類)。target を差し替え、CPL=3 には同じ 1 本の写しを返す。スロット・*/
/*  引数・戻り型・版は不変。                                                 */
/* ======================================================================== */
const char *path_get_drive_user(void) { return tramp_copy(path_get_drive()); }
const char *path_get_cwd_user(void)   { return tramp_copy(path_get_cwd()); }

/* ======================================================================== */
/*  exec_tramp_user_selftest — 写し場の番地とページ属性 (票 T9 §12 R1)       */
/*                                                                          */
/*  kselftest_run() は exec_init() より **前** に走るのでトランポリンページが */
/*  まだ無い。この項だけ kselftest_run_post_exec() から呼ぶ。                */
/*  ビット 0..n が落ちた項目 (0 = 全部通った)。                              */
/* ======================================================================== */
u32 exec_tramp_page_addr(void)
{
    return ring3_tramp_page;
}

/* ======================================================================== */
/*  exec_kapi_layout_selftest — データ欄の固定配置と予約スロット              */
/*  (票 TASK_KAPI_DATA_FIELDS)。exec_init の後に kselftest から呼ぶ。         */
/*  ビット 0..3 が落ちた項目 (0 = 全部通った)。                               */
/* ======================================================================== */
extern i32 __cdecl kapi_reserved_nosys(void);

u32 exec_kapi_layout_selftest(void)
{
    u32 bad = 0;
    const u32 *real = (const u32 *)KAPI_ADDR;
    const u32 *tbl = (const u32 *)ring3_tramp_page;
    u32 i;
    OS32Header h;

    if (ring3_tramp_page == 0) return 0xFu;     /* exec_init より前 */

    /* (0) データ欄は固定オフセット。本物の表とトランポリンの同じ語に値が居る */
    if ((u32)&kapi->sbrk_heap_limit != (u32)KAPI_ADDR + (u32)KAPI_DATA_FIELDS_OFF ||
        real[KAPI_DATA_IDX_SHM_BASE] != kapi->shm_base ||
        tbl[KAPI_DATA_IDX_SHM_BASE] != kapi->shm_base ||
        kapi->shm_base == 0) {
        bad |= 1u << 0;
    }
    /* (1) 本物の表の予約スロットは NULL でなく「未実装」 (CPL=0 は NOSYS)。
     * 関数数 = 容量 (予約 0 本) のとき生成器は kapi_reserved を作らないので、
     * 参照ごと外す (実装レビュー R1: 300 本目でコンパイルが落ちる)。 */
#if KAPI_FUNC_RESERVED > 0
    for (i = 0; i < (u32)KAPI_FUNC_RESERVED; i++) {
        if (kapi->kapi_reserved[i] != kapi_reserved_nosys) bad |= 1u << 1;
    }
    if (kapi->kapi_reserved[0]() != OS32_ERR_NOSYS) bad |= 1u << 1;
#endif
    /* (2) トランポリンの予約スロットは int 0x80 のスタブ (CPL=3 は kill) */
    for (i = (u32)KAPI_FUNC_COUNT; i < (u32)KAPI_FUNC_CAPACITY; i++) {
        const u8 *st = (const u8 *)(ring3_tramp_page + RING3_USTR_STUB_OFF + i * 8u);
        if (tbl[2 + i] != (u32)st || st[0] != 0xB8 || st[1] != (u8)(i & 0xFF) ||
            st[2] != (u8)((i >> 8) & 0xFF) || st[5] != 0xCD || st[6] != 0x80) {
            bad |= 1u << 2;
        }
    }
    /* (3) ヘッダ v3 の照合: v2 → 断る / v3 値違い → 断る / 一致 → 通す */
    kmemset(&h, 0, sizeof(h));
    h.magic = OS32X_MAGIC;
    h.version = 2;
    h.header_size = OS32X_HDR_V2_SIZE;
    if (os32x_layout_check(&h, 4096u, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_OLD)
        bad |= 1u << 3;
    h.version = OS32X_HDR_VERSION;
    h.header_size = OS32X_HDR_SIZE;
    h.kapi_abi_generation = OS32_KAPI_ABI_GENERATION;
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    h.kapi_data_off = (u32)KAPI_DATA_FIELDS_OFF - 4u;
    if (os32x_layout_check(&h, 4096u, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_MISMATCH)
        bad |= 1u << 3;
    h.kapi_data_off = (u32)KAPI_DATA_FIELDS_OFF;
    if (os32x_layout_check(&h, 4096u, KAPI_DATA_FIELDS_OFF) != OS32X_LAYOUT_OK)
        bad |= 1u << 3;
    return bad;
}

u32 exec_tramp_user_selftest(void)
{
    u32 bad = 0;
    u32 addr;
    u32 flags;
    const char *before;
    int saved = ring3_in_syscall;

    if (ring3_tramp_page == 0) return 1u;    /* exec_init より前 */
    addr = ring3_tramp_page + RING3_USTR_OFF;

    /* (0) 写し場は同じ 1 ページの中に収まっている (末尾の 1 バイトまで) */
    if (((addr + RING3_USTR_CAP - 1u) & ~(u32)(PAGE_SIZE - 1)) !=
        ring3_tramp_page) {
        bad |= 1u << 0;
    }
    /* スタブの領域と重ならない */
    if (addr < ring3_tramp_page + RING3_USTR_STUB_OFF +
               (u32)KAPI_FUNC_CAPACITY * 8u) {
        bad |= 1u << 0;
    }

    /* (1) CPL=3 から読める = PTE に PRESENT と USER。無ければ cd / pwd が
     * #PF で畳まれる (この blocker そのもの)。 */
    flags = paging_pte_flags(addr);
    if ((flags & PTE_PRESENT) == 0) bad |= 1u << 1;
    if ((flags & PTE_USER) == 0)    bad |= 1u << 1;

    /* (2) CPL=0 の呼び手には static cwd がそのまま返り、CPL=3 の呼び手には
     * 写しが返る (中身は同じ)。ガードは必ず元へ戻す。 */
    ring3_in_syscall = 0;
    before = vfs_cwd_user();
    if (before != vfs_cwd()) bad |= 1u << 2;
    ring3_in_syscall = 1;
    before = vfs_cwd_user();
    if (before != (const char *)addr) bad |= 1u << 2;
    if (kstrcmp(before, vfs_cwd()) != 0) bad |= 1u << 2;

    /* (3) vfs_devname("/") も CPL=3 には写しの番地を返す (TASK_HDD_INSTALL
     * 段 2: cdinst がカーネルのマウント表を読んで落ちた)。CPL=0 には元の番地 */
    before = vfs_devname_user("/");
    if (before != (const char *)addr || kstrcmp(before, vfs_devname("/")) != 0)
        bad |= 1u << 3;
    ring3_in_syscall = 0;
    if (vfs_devname_user("/") != vfs_devname("/")) bad |= 1u << 3;

    /* (4) path_get_drive / path_get_cwd も同じ */
    ring3_in_syscall = 1;
    before = path_get_drive_user();
    if (before != (const char *)addr || kstrcmp(before, path_get_drive()) != 0)
        bad |= 1u << 4;
    before = path_get_cwd_user();
    if (before != (const char *)addr || kstrcmp(before, path_get_cwd()) != 0)
        bad |= 1u << 4;
    ring3_in_syscall = 0;
    if (path_get_drive_user() != path_get_drive() ||
        path_get_cwd_user() != path_get_cwd())
        bad |= 1u << 4;

    /* (5) WM の文脈 (ring3_wm_depth > 0) ではディスパッチ中でも元の番地が
     * 返る — WM はカーネル帯を直接読める常駐側 (2026-09-26)。 */
    ring3_in_syscall = 1;
    ring3_wm_enter();
    if (vfs_cwd_user() != vfs_cwd() || vfs_devname_user("/") != vfs_devname("/"))
        bad |= 1u << 5;
    ring3_wm_leave();
    if (vfs_cwd_user() != (const char *)addr) bad |= 1u << 5;
    ring3_in_syscall = saved;

    return bad;
}


/* ======================================================================== */
/*  GUI 入力ポンプと強制脱出 (K2 / 契約 T6・T8 の X4)                        */
/* ======================================================================== */

/* IRQ0 が 100Hz で加算するティック (kernel/idt.h)。ポンプの上限判定は
 * これを **読むだけ** で行う (票 K2 の鉄則: get_tick を叩く回数を増やさない)。 */
extern volatile u32 tick_count;

/* ポンプ実行中フラグ (票 K2-3 の再入防止)。ポンプ自身は CPL=0 の WM コード
 * なので int 0x80 は経由しないが、将来ポンプの中から何かが syscall 境界を
 * 通っても二重取り込みにならないようにカーネル側でも止める。 */
static volatile int g_gui_pump_busy = 0;

/* 最後にポンプを回した tick と、その値が有効かどうか。
 * 「同じ tick の間は 1 回だけ」= 前回から 1 tick (10ms) 未満なら呼ばない。 */
static u32 g_gui_pump_tick = 0;
static int g_gui_pump_tick_valid = 0;

/* CPL=3 アプリの強制脱出要求 (CTRL+STOP、契約 T6)。IRQ1 の kbd_irq_handler が
 * 立て、(a) IRQ1 スタブ (割り込まれた文脈が CPL=3 = アプリのコード実行中の
 * とき) と (b) syscall 入口 が見て ring3_fault_kill する。カーネル内 (wrap の
 * 実行中) では畳まない — 中途半端なカーネル状態で longjmp しないため、
 * 要求は残して次の安全な地点で処理する。
 *
 * K5b: 要求は **走っているアプリの AppSlot** に立てる (D4)。IRQ1 の時点で
 * カーネルが知っているのはそれだけで、止めてあるアプリには届かない
 * (そちらは WM が exec_kill で畳む)。 */

/* CTRL+STOP で畳んだ回数 (PM の V4 検証が emu_read_mem で読む)。
 * fault_kill_count にも含まれる (畳む経路は同じ ring3_fault_kill)。 */
volatile u32 ring3_abort_count = 0;

/* ======================================================================== */
/*  ring3_abort_request — CTRL+STOP を受けた (IRQ1 の ISR から呼ばれる)      */
/*                                                                          */
/*  ISR の中では畳まない (EOI も V86 反射も済んでいない)。CPL=3 アプリが      */
/*  走っているときだけ要求を立てる。CUI でシェルしか居ないときは無視。        */
/* ======================================================================== */
void ring3_abort_request(void)
{
    /* 票 T9 §12 S6: GUI 中は宛先 (フォーカス窓の連鎖の末尾、D8) を知って
     * いるのは WM だけなので、カーネルは立てない — 要求は raw リング経由で
     * WM へ届き、WM が exec_abort_clear → exec_kill(末尾) を実行する
     * (決裁 A1)。立てると「そのとき走っていた slot」= WAIT_POLL の sh や
     * 100ms タイマの端末が巻き込まれる (受入 S6 の 2 回目)。
     * 例外は暴走 (APP_RUNAWAY_TICKS 以上 WM へ戻っていない) だけ。
     * CUI 中は 1 バイトも変えない (K2 の唯一の逃げ道)。 */
    if (!appslot_abort_admit(con_sink_is_enabled(), tick_count)) return;
    appslot_abort_request();
}

/* ======================================================================== */
/*  ring3_abort_check — 要求があればアプリを畳む (戻らない)                  */
/*                                                                          */
/*  呼び出し元は 2 か所:                                                     */
/*    - kernel/isr_stub.asm の IRQ1 スタブ (EOI と V86 反射の後、割り込まれた */
/*      文脈が CPL=3 のときだけ)。KAPI を呼ばない計算ループはここで死ぬ。     */
/*    - ring3_syscall_dispatch の入口 (wrap に入る前)。                      */
/*  IRQ 上では資源を触らず移譲し、launch/resume の着地点で IF=1 にして回収。*/
/*  syscall 入口の通常の安全点では共通回収へ直行する (T2a R1)。            */
/* ======================================================================== */
void ring3_abort_check(void)
{
    AppSlot *a = appslot_get(appslot_cur());
    if (!a || !a->abort_req) return;
    a->abort_req = 0;
    if (!g_cur_app) return;         /* CPL=3 アプリはもう居ない */
    ring3_abort_count++;
    ring3_abort_kill();             /* 戻らない */
}

/* ======================================================================== */
/*  ring3_gui_pump — syscall 境界の入力ポンプ (票 K2-1/2/3、契約 T6)         */
/*                                                                          */
/*  アプリが KAPI を呼んでいる限り、WM (gshell) が登録したポンプをここで      */
/*  回し、キーボードとマウスの入力を取りこぼさないようにする。               */
/*                                                                          */
/*  上限 (票 K2-1):                                                          */
/*    - syscall 1 回につき最大 1 回 (この関数はディスパッチャから 1 回だけ    */
/*      呼ばれる)。                                                          */
/*    - 前回から 1 tick (10ms) 未満なら呼ばない。KAPI を毎秒数百回叩く        */
/*      アプリ (v2 PLAN §3 の実測 233/s) で WM の処理が支配的にならないため。 */
/*                                                                          */
/*  除外 (票 K2-3): WM 自身 (owner 1 = シェル帯) からの呼び出し。WM は自分の  */
/*  周期 (X3 = OP_WAIT) で回すので、ここで二重に回さない。                    */
/*                                                                          */
/*  呼ぶ位置は kapi_invoke の **前**、かつフォールトガード                    */
/*  (ring3_in_syscall) の **外側**。ポンプは CPL=0 の WM コードで、ここで      */
/*  落ちるのはアプリのせいではない (契約 T8 の X4: 落ちたらカーネルの責任)。  */
/*  ポンプ実行中も IF=1 のまま — IRQ は普通に入る (int80_stub が sti 済み)。  */
/* ======================================================================== */
static void ring3_gui_pump(void)
{
    GuiPump pump;
    u32 now;

    if (g_gui_pump_busy) return;             /* 再入防止 (票 K2-3) */

    pump = (GuiPump)gui_get_pump();
    if (pump == 0) return;                   /* WM 未登録 (CUI) */

    if (res_owner_get() == GUI_SHELL_OWNER) return;  /* WM 自身の syscall */

    now = tick_count;
    if (g_gui_pump_tick_valid && now == g_gui_pump_tick) {
        return;                              /* 同じ tick 内 = 1 tick 未満 */
    }
    g_gui_pump_tick = now;
    g_gui_pump_tick_valid = 1;

    g_gui_pump_busy = 1;
    /* ここは ring3_in_syscall = 0 の区間だが、WM の文脈である印は同じ形で
     * 立てる (ポンプの位置が動いても 3 つの門の判定が変わらないように)。 */
    ring3_wm_enter();
    pump();
    ring3_wm_leave();
    g_gui_pump_busy = 0;
}

/* ユーザポインタ引数の早期範囲検証 (v2 M2e 補助)。exec が CPL=3 アプリに
 * USER マップした領域 (共有ライブラリ帯/プログラム帯/ユーザスタック/SHM/VRAM)
 * と NULL のみ許可。範囲外 (例: 0xDEADBEEF) は wrap に入る前に弾き、
 * カーネル状態不整合を避ける。
 * 可変長引数はここでは見えないのでフォールトガードが担保する。 */
int ring3_ptr_ok(u32 p)
{
    if (p == 0) return 1;                         /* NULL は wrap 側が処理 */
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
    if (p >= 0xA0000UL && p < 0xC0000UL) return 1;/* VRAM (テキスト/グラフィック) */
    return 0;
}

/* ======================================================================== */
/*  ring3_user_range_ok — 長さまで見るユーザポインタ検証 (票 S0-K §1a)       */
/*                                                                          */
/*  ディスパッチャの早期検証 (kapi_argptr + ring3_ptr_ok) は先頭番地しか見ず、*/
/*  先頭が帯外なら wrap に入る前に kill する (既存挙動。ここでは変えない)。   */
/*  先頭が通った後の「どこまで読んでよいか」はこの関数が決める:              */
/*                                                                          */
/*    - CPL=3 由来 (ring3_in_syscall) のときだけ帯と PTE を見る。常駐シェル / */
/*      gshell の直呼び (CPL=0、ディスパッチャを通らない) は素通し。          */
/*    - 見るのは **帯だけ**。許可帯の中の非 present なページ (guard / sbrk     */
/*      上限〜guard) をカーネルが写すと #PF になるが、それは既存の            */
/*      フォールトガード (ring3_in_syscall) が呼び手を kill する — kprintf の  */
/*      可変長 %s など、他の KAPI と同じ既定の扱い。契約 (FOUNDATION §2-6 の   */
/*      「untrusted pointer / 長さ / 境界を CPL3 経路で検証」) は帯 + 長さで    */
/*      満たす。                                                              */
/*                                                                            */
/*    **PTE (present / USER) は見ない** (2026-09-13、実機 K2 で 2 回失敗):     */
/*      カーネルはページテーブルを「物理番地 = 仮想番地」で読む。ところが      */
/*      PD もアプリ PT も pgalloc から取られ (`MEM_POOL_BASE` は 0x400000 で   */
/*      **アプリ帯そのもの**)、アプリの PD では その仮想番地が per-app 物理へ  */
/*      張り替わっている。つまり syscall 中 (CR3 = アプリ PD) に表を辿ると、    */
/*      PT のつもりでアプリ自身のデータを読む — #PF も起きないまま「非 present」*/
/*      と答える。`AppSlot.as` の控えから引いても、PDE から引いても同じ物理を  */
/*      指すので結果は同じだった。表を正しく歩けるのは master CR3 の下だけで、  */
/*      syscall の途中で CR3 を差し替えるのは割に合わない。                    */
/* ======================================================================== */
/* ---- 検証が断った理由の観測点 (実機 K2 の切り分け、2026-09-13) ----------
 * KAPI にはしない。fault_kill_count と同じくカーネルシンボルとして公開し、
 * `emu_read_mem` で読む。実機で `db_open_existing` が MISUSE を返したとき、
 * 「どのサブ条件で断ったか」がここを読むだけで分かる。
 *   count  : 断った回数
 *   last   : 直前の理由 (RING3_RANGE_*)
 *   addr   : 直前に断ったポインタ / ページ
 *   flags  : そのとき ring3_ptr_ok が見た帯の上端 (RING3_HEAP_TOP)
 * 正常系では 1 バイトも増えない (断ったときだけ書く)。 */
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

/* ======================================================================== */
/*  ring3_user_range_writable — CPL=3 へ**書き込む**前の最後の砦            */
/*                              (票 TASK_HAL_WIRING、Codex 往復 10 / 11)    */
/*                                                                          */
/*  OS32 は **CR0.WP = 0** で走る (`arch/x86/arch_cpu.h` の MMU 有効化。     */
/*  `kernel/shlib.c` が「カーネルからは RO ページにも書ける」ことに依存)。   */
/*  そのため CPL=0 の KAPI ラッパは、アプリが出力引数として渡した**読み取り  */
/*  専用の USER ページ** — 共有ライブラリの `.text` / `.rodata`、全アプリで  */
/*  同じ物理 — にも #PF を起こさずに書けてしまう。早期検査                   */
/*  (`kapi_argptr` → `ring3_ptr_ok`) は帯しか見ないのでここは素通りする。    */
/*  (`ring3_ptr_ok` の「.text への書き込みは PTE が RO なので #PF で捕まる」 */
/*   という注記は CR0.WP = 0 では成り立たない。)                            */
/*                                                                          */
/*  **表の歩き方** (往復 11 で固定):                                        */
/*   - 見るのは**いまのアプリの PD**。`paging_pte_flags()` は master の      */
/*     `page_tables[]` を引くので使えない — アプリでは RW + USER の shlib    */
/*     `.data`/`.bss` が master では USER 無しに見え、**正常な出力を誤って   */
/*     拒否する**。                                                          */
/*   - かといって CR3 = アプリ PD のまま PD/PT の物理番地をポインタとして    */
/*     辿ってもいけない。PD もアプリ PT も pgalloc から取られ               */
/*     (`MEM_POOL_BASE` は 0x400000 = **アプリ帯そのもの**)、アプリの PD では */
/*     その仮想番地が per-app 物理へ張り替わっているので、表のつもりで       */
/*     **アプリ自身のデータ**を読む (2026-09-13、実機 K2 で 2 回失敗。       */
/*     この上の `ring3_user_range_ok` の記録と kernel/paging.h を参照)。     */
/*   - したがって: IF=0 → 現在の CR3 を控える → **master へ切り替える**     */
/*     (master は低位物理を恒等写像しているので、控えたアプリ PD の物理番地を */
/*     そのまま読める) → アプリ PD の PDE → PT の PTE を範囲の全ページで     */
/*     確かめる → **元の CR3 に戻す** → IF を戻す。                         */
/*     **master に居るあいだはユーザー出力に 1 バイトも書かない。**          */
/*                                                                          */
/*  表歩きと USER / RW の判定は paging.c の as_va_to_pa に集約し、          */
/*  ホストで実ソースの組合せを網羅している。                                */
/*  戻り値: 1 = 書いてよい / 0 = 書いてはいけない (呼び手は kill する)。      */
/* ======================================================================== */

/* master CR3 の下でだけ呼ぶこと。pd_phys はアプリ PD の物理先頭。 */
static int ring3_pd_range_writable(u32 pd_phys, u32 p, u32 len)
{
    u32 page, last_page;

    /* 断った理由は ring3_range_refuse で数える (RING3_RANGE_WR_*)。読み側の
     * ring3_user_range_ok と同じ観測点 — 2026-09-26 まで書き側は数えておらず、
     * wrap_mouse_poll で kill されても ring3_range_reject_count が 0 のまま
     * だった。ここは master CR3 の下 (カーネル帯は恒等写像) なので書ける。 */
    if (!pd_phys) return ring3_range_refuse(RING3_RANGE_WR_TABLE, p, 0);
    if (!paging_is_present((uptr)P2V(pd_phys)))
        return ring3_range_refuse(RING3_RANGE_WR_TABLE, p, pd_phys);
    last_page = (p + len - 1u) & ~(u32)(PAGE_SIZE - 1);
    for (page = p & ~(u32)(PAGE_SIZE - 1); ; page += PAGE_SIZE) {
        u32 pa;
        int why = as_va_to_pa(pd_phys, page, &pa);
        if (why == AS_VA_TABLE)
            return ring3_range_refuse(RING3_RANGE_WR_TABLE, p, page);
        if (why == AS_VA_PDE)
            return ring3_range_refuse(RING3_RANGE_WR_PDE, p, page);
        if (why == AS_VA_PTE)
            return ring3_range_refuse(RING3_RANGE_WR_PTE, p, page);

        if (page >= last_page) break;
    }
    return 1;
}

/* 引数の早い段階の門番。1 = 見るまでもなく可 / 0 = 見るまでもなく不可 /
 * -1 = 表を歩いて確かめる。 */
static int ring3_writable_trivial(u32 p, u32 len)
{
    if (len == 0) return 1;
    if (p == 0) return 0;
    if (p + len < p) return 0;            /* 加算の桁あふれ */
    return -1;
}

int ring3_user_ranges_writable(u32 pa, u32 la, u32 pb, u32 lb)
{
    /* **CPL=0 の直呼びは対象外** (常駐シェル / gshell はローカル変数を渡す)。
     * 判定は「呼び出し経路」で決める — CR3 が master かどうかで代用しない
     * (Approve 後の注意 2)。既存の ring3_user_range_ok と同じ門。
     * **WM の文脈 (ring3_wm_depth > 0) も常駐側の直呼び扱い** — WM はアプリの
     * syscall の中で走るので ring3_in_syscall だけでは区別できない
     * (2026-09-26、ring3_wm_depth の注記)。
     * これは**いま渡されたポインタ**の門。アプリが**前に登録した**ポインタは
     * 文脈に関係なく _always で歩く (fs/fd_redirect.c、exec.h の注記)。 */
    if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return 1;
    return ring3_user_ranges_writable_always(pa, la, pb, lb);
}

int ring3_user_ranges_writable_always(u32 pa, u32 la, u32 pb, u32 lb)
{
    unsigned int saved;
    u32 app_cr3, master;
    int ok, ta, tb;

    ta = ring3_writable_trivial(pa, la);
    tb = ring3_writable_trivial(pb, lb);
    if (ta == 0 || tb == 0)
        return ring3_range_refuse(RING3_RANGE_WR_TRIVIAL,
                                  (ta == 0) ? pa : pb, 0);
    if (ta == 1 && tb == 1) return 1;

    /* **2 本を 1 回の往復でまとめて見る** (Approve 後の注意 3)。出力ごとに
     * master を往復すると、時計 1 回あたり CR3 の書き込みが 4 回になる。 */
    saved = irq_save();
    /* CR3 は**この場で自分で読む** — KAPI 入口に控えを取る機構は無く、
     * `g_cur_app->as->pd_phys` は入口で走っていた CR3 とは別物であり得る
     * (Approve 後の注意 1)。共有グローバルには書かない。 */
    app_cr3 = paging_current_cr3() & ~(u32)0xFFFu;
    master  = paging_kernel_pd_phys() & ~(u32)0xFFFu;

    if (app_cr3 == master) {
        ok = (ta != -1 || ring3_pd_range_writable(app_cr3, pa, la)) &&
             (tb != -1 || ring3_pd_range_writable(app_cr3, pb, lb));
    } else {
        paging_load_cr3(master);
        ok = (ta != -1 || ring3_pd_range_writable(app_cr3, pa, la)) &&
             (tb != -1 || ring3_pd_range_writable(app_cr3, pb, lb));
        /* **CR3 を戻してから IF を戻す** (Approve 後の注意 1)。
         * この区間は表を読むだけで、ユーザー出力には 1 バイトも書かない。
         * 検査に落ちたときの kill も、復元を終えた呼び手が行う (注意 5)。 */
        paging_load_cr3(app_cr3);
    }
    irq_restore(saved);
    return ok;
}

int ring3_user_range_writable(u32 p, u32 len)
{
    return ring3_user_ranges_writable(p, len, 0, 0);
}

int ring3_user_range_ok(u32 p, u32 len)
{
    u32 page, last_page;

    /* CPL=0 の直呼び、または WM の文脈 (ring3_wm_depth の注記) */
    if (!ring3_guard_active(ring3_in_syscall, ring3_wm_depth)) return 1;
    if (p == 0) return ring3_range_refuse(RING3_RANGE_NULL, p, 0);
    if (len == 0) return 1;               /* 0 バイトは読まない */
    if (p + len < p) return ring3_range_refuse(RING3_RANGE_OVERFLOW, p, 0);
    if (!g_cur_app)
        return ring3_range_refuse(RING3_RANGE_NO_APP, p, 0);

    last_page = (p + len - 1u) & ~(u32)(PAGE_SIZE - 1);
    for (page = p & ~(u32)(PAGE_SIZE - 1); ; page += PAGE_SIZE) {
        if (page == 0 || !ring3_ptr_ok(page))
            return ring3_range_refuse(RING3_RANGE_BAND, p, page);
        if (page >= last_page) break;
    }
    return 1;
}

#include "ksetjmp.h"

/* ======================================================================== */
/*  sbrk 物理の二段構え (ユーザー決裁 2026-09-11)                            */
/*                                                                          */
/*  K5b-K で per-app 物理にしたとき、heap_size 未指定の CPL=3 プログラムの    */
/*  sbrk に張る物理を最低分 (MEM_EXEC_SBRK_MIN = 256KB) へ固定した。8MB       */
/*  構成で GUI アプリ 1 本を通すためだったが、identity だった頃は帯の残り     */
/*  ぜんぶ (≒1.4MB) が黙って sbrk に使えたので、malloc を多用する CUI        */
/*  プログラム (less 等) が割を食う。                                        */
/*                                                                          */
/*  そこで二段構えにする:                                                    */
/*    段 1 = 従来式。sbrk 上端を guard_a まで伸ばす ([code_end, guard_a) 全部)*/
/*    段 2 = 最低分。sbrk 上端を code_end + MEM_EXEC_SBRK_MIN に落とす        */
/*  段 1 で 3 領域 (本体+sbrk / exec_heap / スタック) + PD + アプリ PT +      */
/*  付随ページ (exec_ring3_extra_pages) が pgalloc の空きに収まるなら段 1、   */
/*  収まらなければ段 2。段 2 でも収まらないときは呼び出し側が                 */
/*  EXEC_ERR_NOMEM を返す (切り詰めない・スワップしない)。段 2 でも付随       */
/*  ページは同じ式に入っているので、sbrk を削って作った空きを使い切らない。   */
/*                                                                          */
/*  heap_size を明示したプログラムはこの分岐に入らない (K5b-K のまま最低分)。 */
/*  要求した exec_heap を必ず渡すのが先で、sbrk を伸ばす余地はそこに無い。    */
/*  どちらの段で走ったかは exec_sbrk_tier_last / exec_sbrk_tier_count[] で    */
/*  後から読める (KAPI にはしない。fault_kill_count と同じカーネルシンボル)。 */
/* ======================================================================== */
/* 3 領域 (本体+sbrk / exec_heap / スタック) の **外** で、この 1 本のために
 * 同じ pgalloc から取るページ数 (K7、2026-09-11)。
 *
 * いまは共有ライブラリの .data/.bss 複製 (shlib_addrspace_attach) だけ。
 * PD とアプリ PT は paging_addrspace_create_n が取るが、それは
 * exec_ring3_pages が疎 PT と lease PT を別に数えている。GFX のバックバッファは
 * 全アプリ共有 (gfx_bb_phys_range) で per-app には取らないので入らない。
 *
 * K7 の実測 (8MB 構成、PM 2026-09-11): アプリ帯の空き 768 ページに対し、
 * 段 1 の 3 領域 + PD + PT が **ちょうど** 768。ここを 0 と見なしていたため
 * 段 1 が空きを使い切り、直後の shlib の 4 ページが取れずに gui_demo が
 * 「shlib data attach failed (out of memory)」で立たなかった。
 * 付随ページを増やすときは必ずここに足すこと — 増やした先で pgalloc から
 * 取るだけだと、勘定に乗らないまま同じ形で落ちる。 */
static u32 exec_ring3_extra_pages(void)
{
    return shlib_data_pages();
}

static u32 exec_ring3_pages(u32 load_base, u32 sbrk_end, u32 exec_heap_size,
                            u32 stack_size)
{
    return (sbrk_end - load_base + exec_heap_size + stack_size) / PAGE_SIZE
         + 2 /* PD + first lease PT */
         + ((sbrk_end - 1) >> 22) - (load_base >> 22) + 1
         + ((MEM_EXEC_HEAP_BASE + exec_heap_size - 1) >> 22) - (MEM_EXEC_HEAP_BASE >> 22) + 1
         + ((MEM_APP_STACK_TOP - 1) >> 22) - ((MEM_APP_STACK_TOP - stack_size) >> 22) + 1
         + exec_ring3_extra_pages();
}

/* 選んだ段 (1 or 2) を返し、*sbrk_end に sbrk の上端を書く。 */
static int exec_sbrk_pick_tier(u32 load_base, u32 code_end, u32 guard_a,
                               u32 exec_heap_size, u32 stack_size,
                               u32 free_pages, u32 *sbrk_end)
{
    u32 lo = code_end + MEM_EXEC_SBRK_MIN;

    if (lo > guard_a) lo = guard_a;
    if (exec_ring3_pages(MEM_EXEC_LOAD_ADDR,
            MEM_EXEC_LOAD_ADDR + guard_a - load_base, exec_heap_size, stack_size)
            <= free_pages) {
        *sbrk_end = guard_a;
        return 1;
    }
    *sbrk_end = lo;
    /* 帯の残りが最低分より狭いなら、落としても従来式と同じものを張っている。 */
    return (lo >= guard_a) ? 1 : 2;
}

/* ======================================================================== */
/*  app_map_region — アプリ帯の 1 領域を per-app 物理で張る (D1)             */
/*                                                                          */
/*  まず連続で取り (P1 の map_user_range_phys)、断片化で取れなければ          */
/*  ページ単位に倒す。per-app 物理にした副産物で連続は必須ではない —          */
/*  K5a の申し送り D10 が「断片化が出たらページ単位へ倒せ」と書いた点。       */
/*  途中で尽きたら -1。張り終えたぶんは呼び出し側の巻き戻し                   */
/*  (exec_teardown_app → paging_addrspace_free_user_range) が PTE を辿って   */
/*  返すので、ここで部分解放はしない。                                        */
/* ======================================================================== */
static int app_map_region(struct addrspace *as, u32 vstart, u32 vend)
{
    u32 pages, phys, v;

    if (vstart >= vend) return 0;
    pages = (vend - vstart) / PAGE_SIZE;

    phys = pgalloc_alloc_phys(as->owner, (int)pages);
    if (phys) {
        kmemset(P2V(phys), 0, pages * PAGE_SIZE);
        if (paging_addrspace_map_user_range_phys(as, vstart, vend, phys,
                                                 PAGE_RW | PTE_USER) == 0) {
            return 0;
        }
        pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, (int)pages);
        return -1;
    }

    for (v = vstart; v < vend; v += PAGE_SIZE) {
        phys = pgalloc_alloc_phys(as->owner, 1);
        if (!phys) return -1;
        kmemset(P2V(phys), 0, PAGE_SIZE);
        if (paging_addrspace_map_user(as, v, phys, PAGE_RW | PTE_USER) != 0) {
            pgalloc_free_n_owner(as->owner, phys / PAGE_SIZE, 1);
            return -1;
        }
    }
    return 0;
}

/* ======================================================================== */
/*  exec_map_shared_bb — 共有のバックバッファをアプリ PD へ USER で写す      */
/*                                                                          */
/*  BB は**恒等 (仮想 = 物理) のまま丸ごと**写す — gfx_get_framebuffer() が   */
/*  返す番地・pegc_init のクリア・libos32gfx / shlib の Painter が全部この    */
/*  番地で BB を触るので、番地を変えたり一部を写さなかったりはできない。     */
/*  map_user_range ではなく **_keep** — Cirrus のクライアント面は PCD 付きの  */
/*  デバイス窓で、flags をそのまま書くと PCD が落ちる (レビュー #5 ②③)。     */
/*                                                                          */
/*  BB が私有領域 [MEM_APP_BAND_BASE, user_top) と重なるなら写さずに -1。     */
/*  ring3_band_set が私有領域の上端を sys_usable_mem_end() で止めているので   */
/*  通常は起きない (8MB + PEGC の BB [0x7B5000, 0x800000) は user_top の上)。 */
/*  重なったまま写すと私有ページの PTE を BB の物理で上書きし、teardown で   */
/*  戻らなくなる (2026-09-30 の 74 ページ漏れ) ので、黙って写すより断る。    */
/*  帯の中でも私有領域の上 (8MB + PEGC) はアプリ固有 PT に、帯の外はその PD  */
/*  が master と共有する PT に書く — どちらも teardown が返す 3 領域の外。   */
/* ======================================================================== */
/* Copy launch data through physical backing while the master PD is active. */
/* Read the launcher's bytes after switching to master; no high-VA dereference. */
static const char *exec_image_reject_reason(const OS32Header *hdr, int is_shell,
                                            u32 load_base, u32 max_size)
{
    const char *reason = 0;
    if (hdr->load_addr != load_base) reason = "Error: invalid image load address\n";
    else if (hdr->flags & OS32X_FLAG_SHLIB) reason = "Error: shared library is not executable\n";
    else if (!hdr->text_size || hdr->entry_offset >= hdr->text_size ||
             hdr->text_size > max_size || hdr->bss_size > max_size - hdr->text_size)
        reason = "Error: invalid image entry or range\n";
    else if (is_shell && hdr->shlib_protocol)
        reason = "Error: resident shell must have shlib_protocol=0\n";
    else if (hdr->shlib_protocol && !shlib_loaded())
        reason = "Error: required shared library is not loaded\n";
    return reason;
}

static int exec_stack_bytes(u32 requested, u32 command_bytes, u32 *bytes)
{
    u32 size = MEM_EXEC_STACK_SIZE;
    if (requested) {
        if (requested > 0x7fffffffUL - (PAGE_SIZE - 1)) return -1;
        size = PAGE_ALIGN_UP(requested);
        if (size < MEM_APP_STACK_MIN) size = MEM_APP_STACK_MIN;
    }
    if (size > MEM_APP_STACK_TOP - MEM_EXEC_HEAP_BASE - PAGE_SIZE ||
        command_bytes > size - OS32_MAX_ARGS * sizeof(u32) - 64) return -1;
    *bytes = size;
    return 0;
}

static u8 launch_read_byte(u32 pd, const char *p, int *failed)
{
    u32 pa = (u32)p;
    if ((u32)p >= MEM_APP_BAND_BASE && (u32)p < MEM_LEASE_END &&
        as_va_to_pa_read(pd, (u32)p, &pa)) {
        *failed = 1;
        return 0;
    }
    return *(const u8 *)P2V(pa);
}

static int app_store(AppSlot *a, u32 va, const void *src, u32 len)
{
    const u8 *p = src;
    while (len) {
        u32 pa = va, n = PAGE_SIZE - (va & (PAGE_SIZE - 1));
        if (n > len) n = len;
        if (a->cpl3 && as_va_to_pa(a->as->pd_phys, va, &pa)) return -1;
        kmemcpy(P2V(pa), p, n);
        va += n; p += n; len -= n;
    }
    return 0;
}

static int exec_bb_overlaps_user(u32 bb_base, u32 bb_size, u32 user_top)
{
    u32 bb_end;
    if (!bb_size || bb_base > ~0UL - bb_size) return 0;
    bb_end = bb_base + bb_size;
    return (bb_base < user_top && bb_end > MEM_APP_BAND_BASE) ? 1 : 0;
}

static int exec_map_shared_bb(struct addrspace *as, u32 user_top)
{
    u32 bb_base = 0, bb_size = 0;

    gfx_bb_phys_range(&bb_base, &bb_size);
    if (exec_bb_overlaps_user(bb_base, bb_size, user_top)) return -1;
    if (bb_size)
        paging_addrspace_map_user_keep(as, bb_base, bb_base + bb_size,
                                       PAGE_RW | PTE_USER);
    return 0;
}

/* ======================================================================== */
/*  exec_teardown_app — CPL=3 アプリの物理とアドレス空間を返す (D1/D4)       */
/*                                                                          */
/*  **master CR3 に戻してから**呼ぶこと (破棄する PD がアクティブだと         */
/*  paging_addrspace_destroy が何もせずに戻る)。                             */
/*  返すのはこのアプリ帯の 3 領域だけ:                                        */
/*    [load_addr, sbrk_heap_limit)        本体 + data + bss + sbrk (最低分)   */
/*    [exec_heap_base, +exec_heap_size)   exec_heap                          */
/*    [band_top - stack, band_top)        ユーザスタック                     */
/*  ガードページ (guard_a / guard_b) は「張っていない = 非 present」なので     */
/*  返すものが無い。共有帯 (VRAM / SHM / フォント / GFX / トランポリン) は     */
/*  paging_addrspace_free_user_range がアプリ固有 PDE の外を触らないので       */
/*  巻き添えにならない。band_top は**私有領域の上端** (ring3_band_set) で、    */
/*  帯の中でもその上にある共有 BB (8MB + PEGC の [0x7B5000, 0x800000)) は     */
/*  3 領域のどれにも入らない — PTE を辿って BB の物理を返そうとはしない。    */
/* ======================================================================== */
/* 取り残し (3 領域の外に張られたまま返らなかったページ) の累計。R5 (a) の
 * 診断で、0 のままが正常 (カーネルシンボル。KAPI にはしない)。 */
u32 exec_as_leftover_pages;
u32 exec_entry_calls; /* D35: rejected images leave this counter unchanged. */

static void exec_teardown_app(AppSlot *a)
{
    u32 left;
    if (!a || !a->cpl3 || !a->as || !a->as->pd_phys) return;
    /* 共有ライブラリの .data 複製ページを返す (PD 破棄の前, K3) */
    lease_revoke_all(a->as);
    shlib_addrspace_detach(a->as);
    if (a->sbrk_heap_limit > a->load_addr)
        paging_addrspace_free_user_range(a->as, a->load_addr,
                                         a->sbrk_heap_limit);
    if (a->exec_heap_size)
        paging_addrspace_free_user_range(a->as, a->exec_heap_base,
                                         a->exec_heap_base + a->exec_heap_size);
    paging_addrspace_free_user_range(a->as, a->stack_base, a->stack_top);
    paging_addrspace_destroy(a->as);
    /* 最後に AS owner の取り残しを台帳から掃除し (件数は診断へ)、番号を返す
     * (TASK_T1_LEDGER §3-2、R5 (a))。 */
    left = 0;
    if (ledger_reclaim_owner(a->as->owner, &left))
        exec_as_leftover_pages += left;
    (void)ledger_owner_retire(a->as->owner);
    kfree(a->as);
    a->as = 0;
    a->cpl3 = 0;
}

/* V86 バッキングの owner (TASK_T1_LEDGER §4-8): KAPI 経由 (CPL=3 アプリの
 * syscall の中) なら呼び手の AS、カーネルの中からなら kernel。 */
u32 exec_ledger_owner(void)
{
    return (g_cur_app && g_cur_app->cpl3) ? g_cur_app->as->owner
                                         : LEDGER_OWNER_KERNEL;
}

/* ======================================================================== */
/*  exec_restore_context — 「現在のプログラム」を id のものに切り替える       */
/*                                                                          */
/*  票 D1 の I7 / I9 / I10 — 仮想レイアウトは動かさず、カーネル側の           */
/*  「いま走っているのは誰か」を表す値だけを差し替える:                       */
/*    CR3 / アプリ帯の上端 / exec_heap の管理変数 / sbrk 上限 (本物の表と      */
/*    トランポリンの両方) / exec ネスト段。                                   */
/*  CPL=0 のプログラム (シェル / --cpl0 の子) のガードは master の identity    */
/*  ページなので張り直す。CPL=3 アプリのガードはアプリ PT ごと捨てるので       */
/*  何もしない (I8)。                                                        */
/* ======================================================================== */
static void exec_restore_context(int id)
{
    AppSlot *a = appslot_at(id);
    if (!a) return;

    if (a->cpl3 && a->as->pd_phys) {
        g_cur_app = a;
        ring3_band_set(a->band_pdes);
        paging_load_cr3(a->as->pd_phys);
    } else {
        g_cur_app = 0;
        ring3_band_set(1);
        paging_load_cr3(paging_kernel_pd_phys());
    }

    if (a->exec_heap_base != 0) {
        /* exec_heap_init_at ではなく restore_state。init_at はヒープ先頭に
         * 空きブロックヘッダを書き直してしまい、親が子の起動前に確保して
         * いたブロックのヘッダを壊す ("bad magic feeefeee" の正体)。 */
        exec_heap_restore_state(a->exec_heap_base, a->exec_heap_size,
                                a->exec_heap_used);
    }
    kapi->sbrk_heap_limit = a->sbrk_heap_limit;
    if (a->cpl3) {
        ((u32 *)ring3_tramp_page)[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = a->sbrk_heap_limit;
    }
    if (!a->cpl3 && a->guard_a != 0) {
        paging_set_not_present(a->guard_a, a->guard_a + PAGE_SIZE - 1);
        paging_set_not_present(a->guard_b, a->guard_b + PAGE_SIZE - 1);
    }
    exec_nest_level = a->depth;
}

/* ======================================================================== */
/*  exec_reclaim_owned — この ID が持っている資源だけを回収する (D3)         */
/*                                                                          */
/*  かつては「exec のネスト段」で回していた並びを、そのまま **アプリ ID** で  */
/*  回すようにしたもの。7 種のうち shm と db は所有者を見ていなかったので     */
/*  shm_free_owned / db_cleanup_owned へ差し替えてある (D3 の表、P3/P5)。     */
/*  ここを段のまま残すと、アプリ A の終了がアプリ B の SHM や DB 接続を       */
/*  巻き上げる (4 本同時では実際に起きる)。                                   */
/* ======================================================================== */
static void exec_reclaim_resources(int id)
{
    /* (1) SQLite DB リソース (P5: cleanup_all → cleanup_owned)。
     * **FD 回収より先** (票 S0-K §1c / F2 の順序修正)。close は未 commit の
     * rollback で journal を読み書きするので、そのときまだ main / journal の
     * FD が生きていなければならない。ここを (6) に置いていた間は、
     * vfs_close_owned が先に FD を閉じ、SQLite が閉じた FD 番号へ遅延 close /
     * rollback を投げる形になっていた。他の相対順は 1 つも動かさない。 */
    db_cleanup_owned(id);
    /* (2) 標準FDのリダイレクト解除 (ファイルFDも自動クローズ)。
     * 表は FD 0/1/2 の 3 本しかないので、2 本のアプリが同時に stdout を
     * リダイレクトすることはできない (D3 の限界。GUI アプリは使わない)。 */
    fd_redirect_reset_owned(id);
    /* (3) FD自動クローズ (この ID が open した FD 3 以上)。
     * カーネル常駐FD (vfs_fd_set_protect で保護) は除外される。 */
    vfs_close_owned(id);
    /* (4) パイプバッファ自動解放 */
    pipe_free_owned(id);
    /* (5) 共有メモリ (P3: 所有者付きになった) */
    shm_free_owned(id);
    /* (6) サウンド: この ID の退避済み音だけを捨てる (D9-4)。
     * 鳴っているのがこの ID なら止める。他のアプリの音は無事。 */
    snd_owner_exit(id);
    /* (6b) PCM: この ID が鳴らしていれば待たずに止めて資源を返す
     * (票 TASK_PCM_CS4231 §2-1「所有と回収」)。保存した owner と id だけを
     * 照合する — 現在 owner に依存せず、私有ページ返却の前に止める。 */
    pcm_reclaim(id);
}

static void exec_notify_owned(int id)
{
    /* (7) GUI リソース回収 (契約 T4 / U8)。WM がこの owner のウィンドウ・
     * サーフェス・タイマ・スロットを回収する。畳む 3 経路すべてが
     * ここを通るので、WM は 1 か所で回収できる。 */
    gui_owner_exit(id);
    /* (8) 打鍵の注入リング (票 K7 D5)。注ぎ手は con_sink の読み手 1 本なので、
     * 畳んだのがその 1 本なら溜まっている打鍵を捨てる。**con_sink の所有を
     * 返す前**に呼ぶこと — 照合に g_reader を使うため。 */
    kbd_inject_owner_exit(id);
    /* (9) 子の終了を端末へ知らせる EXIT レコード (票 T7 E1)。gshell 配下で
     * 走った CUI プログラムが畳まれたことは、con_sink を読んでいる端末が
     * 他に知る手がない (出力が止まるだけ) ので、ここで 1 本積む。
     * 積まない 3 つ: シェル自身の終了 (端末の親を「子の終了」と言わない)、
     * CUI モード中 (シンク無効 — 端末は居ない)、そして **読み手本人の退場**
     * (受け取る相手がもう居ない)。読み手の照合は g_reader を使うので、
     * 所有を返す (10) の **前** でなければ端末自身の退場でも積んでしまう。 */
    if (id != APP_ID_SHELL && con_sink_is_enabled() &&
        con_sink_reader_get() != id) {
        con_sink_push_exit(id);
        /* (9a) 端末**配下の子**が畳まれたら、その子の stdin に宛てて端末が
         * 注入リングへ積んだ打鍵 (票 N4 の貼り付け) の残りを捨てる。読み手
         * (端末) の退場は (8) が扱うが、子の退場では (8) の照合が外れて残る
         * ため、放っておくと次の子が食う (打鍵でも起きる既存挙動)。注ぎ手
         * (端末) は生きているので con_sink の所有規則には触れず内容だけ捨てる。
         *   捨てるのは **退場したのが読み手 (端末) の子のときだけ** — 無関係な
         * GUI アプリが畳まれても発火させない (N4a 実装レビュー B1 の退行修正)。
         * launch_child は不正 ID / 読み手不在 (CON_SINK_NO_READER) で 0 を返す
         * ので、読み手が居なくても id と一致せず安全。 */
        if (launch_child(con_sink_reader_get()) == id) {
            kbd_inject_discard();
        }
    }
    /* (9b) 起動要求表 (票 T9 D3)。**ID だけを使う** — 正常終了は AppSlot を
     * 解放した後にここへ来る (exec_kill も同じ)。空のスロットの欄を
     * 回収通知に使ってはいけない。
     *   child == id の表  : その要求は終わった (DONE + child = 0)
     *   requester == id の表: 要求者が退場した。子が残っていれば「孤児回収」
     *     (KILL の PENDING) として WM の top-level に渡す — カーネルはここから
     *     kill しない (回収文脈では CR3 も段も動かせない)。 */
    launch_owner_exit(id);
    /* (9c) Host Services のハンドル (票 N1 / TASK_N0 §1a)。owner が握ったまま
     * 畳まれたハンドルを内部解放する — RELEASE も送るので、Agent 側の受付枠
     * (ACTIVE は rid ごと 2 件) が埋まったままにならない。公開 API を通さず
     * ID を指定して解放する。 */
    host_owner_exit(id);
    /* (10) console シンクの読み手 (票 K6C)。読み手は 1 本だけなので、畳んだ
     * のがその 1 本なら所有を返す — 返さないと次の端末アプリが永久に
     * OS32_ERR_EXIST を食う。リングの中身は捨てない (GUI は続いており、
     * 次の読み手が拾えばよい)。 */
    con_sink_owner_exit(id);
    /* (11) 画面の所有者 (票 T8 D1)。全画面 GFX を握ったまま畳まれた
     * (正常終了 / kill / fault / CTRL+STOP のどれでも) ら WM へ返す。
     * WM は exec_start / exec_resume から戻った直後に gfx_screen_owner()
     * を見て、1 に戻っていれば復帰の描き直しに入る。 */
    appslot_gfx_owner_exit(id);
}

static void exec_reclaim_owned(int id)
{
    exec_reclaim_resources(id);
    exec_notify_owned(id);
}

/* ======================================================================== */
/*  exec_launch_abort — 起動途中で失敗したときの唯一の巻き戻し口             */
/*                                                                          */
/*  K5b で構成が単純になった: 起動は **最後まで失敗しうる操作を済ませてから** */
/*  appslot_start_commit する (= owner / 段 / ヒープの切り替えは iret の      */
/*  直前 1 か所) ので、ここで戻すのは 3 つだけ:                              */
/*    (1) アプリの物理ページとアドレス空間 (取れていれば)                     */
/*    (2) CR3 と「現在のプログラム」を起動元へ                               */
/*    (3) スロットを空へ                                                     */
/*  起動失敗では所有者回収を回さない — 子のコードは 1 命令も走っておらず、    */
/*  この ID のタグを持つ資源は存在し得ない (模型ケース 10)。                  */
/* ======================================================================== */
static int exec_launch_abort(int launcher_id, int id, int status)
{
    AppSlot *a = appslot_at(id);
    if (a) {
        /* スロットはまだ commit していない (state は FREE のまま) ので、
         * 返すのはアプリの物理とアドレス空間だけ。回収カウンタも動かさない。 */
        paging_load_cr3(paging_kernel_pd_phys());
        exec_teardown_app(a);
        a->pages = 0;
    }
    exec_restore_context(launcher_id);
    return status;
}

/* 起動を諦めるときに「アプリ帯の上端」だけを起動元の値へ戻す。
 * exec_restore_context を使うと exec_heap の管理変数まで巻き戻してしまい、
 * まだ save していない起動元のヒープ使用量が古い値で上書きされる。 */
static void exec_restore_band(int id)
{
    AppSlot *a = appslot_at(id);
    ring3_band_set((a && a->cpl3) ? a->band_pdes : 1);
}

/* longjmp する側がスロットを空にするので、jmpbuf は先に控える。 */
static u32 g_exit_jmpbuf[KSETJMP_BUF_LEN];

/* ======================================================================== */
/*  exec_exit — 現在のプログラムを畳み、その ID の呼び出し元へ戻る            */
/*                                                                          */
/*  畳むのは常に「いま走っている 1 本」だけ (D4)。正常終了・fault・          */
/*  CTRL+STOP の 3 経路が全部ここを通る。 */
/* ======================================================================== */
static void exec_finish(int id, int status, int kind)
{
    AppSlot *a = appslot_get(id);
    int parent = APP_ID_SHELL;
    u32 k;

    if (!a) return;
    exec_exit_status = status;

    /* 票 TASK_EXIT_STATUS §2-1: 種別は **呼び手** が決める。ここで status から
     * 推測してはいけない (exit(-2) と fault が同じ値になる)。
     * GUI 経路の子 (exec_start / exec_resume = a->gui) は記録しない — 同期
     * 起動の結果と混ざる (受入 S15)。 */
    if (!a->gui) {
        g_last_kind = kind;
        g_last_code = status;
    }

    /* 後始末とシェル復帰は master PD 上で行う。 */
    if (g_cur_app) {
        paging_load_cr3(paging_kernel_pd_phys());
    }

    /* CPL=0 のプログラムだけがカーネルの identity ページを触っている。
     * CPL=3 アプリのガードとヒープはアプリ PT ごと捨てるので不要 (I8)。 */
    if (!a->cpl3) {
        if (a->guard_a != 0) {
            paging_set_page(a->guard_a, a->guard_a, PAGE_RW);
            paging_set_page(a->guard_b, a->guard_b, PAGE_RW);
        }
        if (a->exec_heap_base != 0 && id != APP_ID_SHELL) {
            exec_heap_reset();
        }
    }

    for (k = 0; k < KSETJMP_BUF_LEN; k++) g_exit_jmpbuf[k] = a->jmpbuf[k];

    /* WM 通知より先に親の文脈へ戻す。gui_owner_exit() は
     * WM (gshell) のコードで、そこで KAPI の mem_alloc を踏むと exec_heap が
     * 「畳んだアプリの仮想ヒープ」を指したままになる — その物理はもう
     * pgalloc へ返しているので、master CR3 の下で他人のページを書きに行く。
     * 回収は全部 ID を明示して呼ぶので、owner を先に戻しても取りこぼさない。 */
    if (id == APP_ID_SHELL) {
        /* シェル自身の終了 (K4 のシェル起動ループへ戻る)。従来どおり
         * 段 0 / owner 0 に落として exec_run(shell) の setjmp 点へ帰る。 */
        g_cur_app = 0;
        ring3_band_set(1);
        exec_nest_level = 0;
        res_owner_set(0);
        exec_reclaim_owned(id);
    } else {
        parent = appslot_return_target(id);
        /* 装置・FD の利用終了を私有ページの返却より先に済ませる (R1)。 */
        exec_reclaim_resources(id);
        exec_teardown_app(a);
        appslot_reclaim(id);
        /* 終了に伴う master 復帰は「生存アプリの集合が変わる瞬間」= G7 の
         * 切替ではない。appslot_switch_to が transition_count で別勘定する。 */
        appslot_switch_to(parent);
        exec_restore_context(parent);
        exec_notify_owned(id);
    }
}

void exec_exit(int status, int kind)
{
    exec_finish(appslot_cur(), status, kind);
    g_longjmp_reason = EXEC_LJ_EXIT;
    g_longjmp_id = 0;
    exec_longjmp(g_exit_jmpbuf);
}

/* IRQ/例外では資源に触れず、対象と種別を控えて trusted stack へ移譲。 */
static void exec_pending_transfer(int kind)
{
    int id = appslot_cur();
    AppSlot *a = appslot_get(id);
    if (!a || g_pending_id) { for (;;) { _stop(); } }
    g_pending_id = id;
    g_pending_kind = kind;
    a->state = kind == EXEC_KIND_ABORTED ? APP_STATE_ABORT_PENDING :
                                         APP_STATE_FAULT_PENDING;
    g_longjmp_reason = EXEC_LJ_PENDING;
    exec_longjmp(a->jmpbuf);
}

/* launch と resume の両 setjmp 着地からだけ呼ぶ。park は消費しない。 */
static void exec_pending_finish(void)
{
    int id, kind;
    if (g_longjmp_reason != EXEC_LJ_PENDING) return;
    if (kctx_irq_depth || kctx_exc_depth) { for (;;) { _stop(); } }
    id = g_pending_id;
    kind = g_pending_kind;
    g_pending_id = 0;             /* callback / 再 longjmp より先に一度だけ消費 */
    g_longjmp_reason = EXEC_LJ_EXIT;
    g_longjmp_id = 0;
    paging_load_cr3(paging_kernel_pd_phys());
    _enable();
    exec_finish(id, EXEC_ERR_FAULT, kind);
}

void exec_fault_recover(void)
{
    exec_pending_transfer(EXEC_KIND_FAULT);
}

void __cdecl kapi_sys_exit(int status)
{
    /* CPL=3 (リング3) アプリからの正常終了 (トランポリン経由, v2 M2)。
     * master CR3 復帰・AS 破棄・per-app 物理の返却は exec_exit が ID 単位で
     * 行う。CPL=0 プログラム (シェル等) は g_cur_app が 0 なので従来どおり。 */
    ring3_in_syscall = 0;   /* syscall(sys_exit) を抜ける — ガードを下ろす */
    ring3_wm_depth = 0;
    exec_exit(status, EXEC_KIND_EXITED);
}

/* ======================================================================== */
/*  ring3_syscall_dispatch — CPL=3 からの int 0x80 ディスパッチャ (v2 M2d)   */
/*                                                                          */
/*  kernel/ring3_entry.asm の int80_stub が pushad 後のフレーム先頭を渡す。  */
/*  フレーム (u32 配列, pushad + CPU が積んだ例外フレーム):                   */
/*    [0..7]=pushad (EDI,ESI,EBP,ESP,EBX,EDX,ECX,EAX)  → EAX=[7]             */
/*    [8]=EIP [9]=CS [10]=EFLAGS [11]=userESP [12]=userSS                    */
/*                                                                          */
/*  eax(=[7]) がスタブの積んだ slot。範囲外は即 kill (CONTRACTS C4)。        */
/*  本物の KAPI 表 (KAPI_ADDR: [magic][version][fn0..]) から wrap を引き、    */
/*  ユーザスタック (userESP+4, スタブの ret アドレス分を飛ばす) から引数を    */
/*  コピーして呼ぶ。戻り値は eax スロット([7])へ書く → popad で復元される。   */
/*  現 CR3 はアプリ PD のまま呼ぶ (ユーザポインタ引数がアプリ帯で解決される)。*/
/*  sys_exit は wrap → kapi_sys_exit が teardown+longjmp するのでここへ戻らない。*/
/*                                                                          */
/*  K5b: フレーム先頭を g_cur_frame に控える。gshell の op_wait が            */
/*  exec_park() を呼んだとき、この 13 語をそのまま AppSlot へ写して           */
/*  「CPL=3 の続き」を保存する (D2 の (b): カーネルスタックは 1 本のまま)。   */
/* ======================================================================== */

/* 可変長引数 (kprintf) を拾うためのコピー窓 (固定分より広めに取る)。 */
#define RING3_ARG_WINDOW  64u

void __cdecl ring3_syscall_dispatch(u32 *frame)
{
    u32 slot     = frame[7];         /* スタブが積んだ slot (eax) */
    u32 user_esp = frame[11];        /* CPL=3 の ESP (int が積んだ) */
    const void *args_src;
    u32 nbytes;
    u32 window;
    u32 wrapptr;
    u32 *prev_frame = g_cur_frame;

    g_cur_frame = frame;
    /* WM の文脈の深さは **CPL=3 の syscall の入口で必ず 0** (不変条件)。
     * WM を longjmp で抜けた (park / kill) 後に 1 が残っていても、ここで
     * 立ち直る — 残ると 3 つの門がアプリのポインタを素通しする穴になる。 */
    ring3_wm_depth = 0;
    /* 暴走判定の起点 (票 T9 §12 S6b)。**代入 1 つだけ** — ここは hot path。
     * start / resume だけを起点にしていると、GetMessage 型の GUI アプリ
     * (端末) が WM の op_wait の中で待っている間は更新されず、2 秒待った
     * だけで「暴走」に見えた。KAPI を呼んでいる限りここを通る。 */
    if (g_cur_app) g_cur_app->last_kernel_tick = tick_count;

    /* --- CTRL+STOP の要求があればここで畳む (契約 T6) --- */
    ring3_abort_check();        /* 要求があれば longjmp して戻らない */

    /* --- GUI 入力ポンプ (票 K2-1、契約 T6 / T8 の X4) ---
     * フォールトガード (ring3_in_syscall) を立てる **前** に回す。 */
    ring3_gui_pump();

    /* 範囲外 slot はワイルド呼び出し → アプリだけ kill (カーネルを飛ばさない)。 */
    if (slot >= (u32)KAPI_FUNC_COUNT) {
        ring3_fault_kill();
    }

    /* 本物の表から wrap_<slot> を取得 ([magic][version] の後が fn 表)。 */
    wrapptr = ((const u32 *)KAPI_ADDR)[2 + slot];
    args_src = (const void *)(user_esp + 4u);
    nbytes = (u32)kapi_argsize[slot];

    /* --- (核) フォールトガードを立てる (v2 M2e) --- */
    ring3_in_syscall = 1;

    /* --- (補助) 明示ポインタ引数の早期範囲検証 (v2 M2e) --- */
    {
        u16 ptrmask = kapi_argptr[slot];
        if (ptrmask) {
            const u32 *a = (const u32 *)args_src;
            u32 nfixed = nbytes / 4u;   /* 固定引数の個数 */
            u32 k;
            for (k = 0; k < nfixed && k < 16u; k++) {
                if ((ptrmask & (u16)(1u << k)) && !ring3_ptr_ok(a[k])) {
                    ring3_fault_kill();   /* 範囲外ポインタ → kill、戻らない */
                }
            }
        }
    }

    /* 引数コピー窓: 固定分 + 可変長(kprintf)のため広めに取り、ユーザスタック
     * 上端でクランプして over-read #PF を避ける。 */
    window = (nbytes < RING3_ARG_WINDOW) ? RING3_ARG_WINDOW : nbytes;
    if ((u32)args_src < RING3_USTACK_TOP &&
        window > g_cur_app->stack_top - (u32)args_src) {
        window = g_cur_app->stack_top - (u32)args_src;
    }

    /* 本物の wrap を呼ぶ (現 CR3 = アプリ PD)。戻り値を eax スロットへ。
     * gui_call(OP_WAIT) → exec_park() はここから longjmp して戻らない。 */
    frame[7] = kapi_invoke((void *)wrapptr, args_src, window);

    /* 正常復帰: ガードを下ろす */
    ring3_in_syscall = 0;
    g_cur_frame = prev_frame;

    /* --- 出口でも CTRL+STOP を見る (契約 T6、v1.2 G2 で実測した隙間) --- */
    ring3_abort_check();
}

/* ======================================================================== */
/*  ring3_fault_kill — CPL=3 由来の #PF/#GP でアプリを kill (v2 M1e)        */
/*                                                                          */
/*  #PF/#GP ハンドラ (kernel/isr_handlers.c) がフォールトフレームの         */
/*  CS.RPL=3 (= CPL=3 由来) を検出したときに呼ぶ。カーネルを巻き込まず       */
/*  **その ID だけ**を畳んでシェル (WM) に戻す (D4)。                        */
/*  資源を触らず longjmp。両着地点の exec_pending_finish が通常文脈で回収。 */
/*  この関数は longjmp するので戻らない。                                    */
/* ======================================================================== */
static void ring3_kill_kind(int kind)
{
    fault_kill_count++;
    if (kind == EXEC_KIND_FAULT && ring3_wm_depth > 0) {
        ring3_wm_fault_count++;     /* 深さを 0 に戻す前に数える */
    }
    ring3_in_syscall = 0;   /* syscall 途中で畳む場合も必ずガードを下ろす */
    ring3_wm_depth = 0;     /* WM の中から畳んだ場合も深さを戻す (出口を通らない) */
    /* syscall 入口などの通常の安全点は回収へ直行。IRQ/例外だけ移譲。 */
    if (!kctx_irq_depth && !kctx_exc_depth)
        exec_exit(EXEC_ERR_FAULT, kind);
    else
        exec_pending_transfer(kind);
}

void ring3_fault_kill(void)
{
    ring3_kill_kind(EXEC_KIND_FAULT);   /* 戻らない */
}

/* CTRL+STOP で畳む口 (票 §1 事実 15)。後始末は fault とまったく同じで、
 * 違うのは記録する種別だけ — ランナーが「時間切れで畳んだ」と「落ちた」を
 * 分けられるようにする。 */
void ring3_abort_kill(void)
{
    ring3_kill_kind(EXEC_KIND_ABORTED);   /* 戻らない */
}

/* ======================================================================== */
/*  ring3_resume — 保存した CPL=3 フレームへ戻る (P4、kernel/ring3_entry.asm)*/
/*  cli → TSS.ESP0 → CR3 → フレームを積んで popad; iretd を割り込み禁止で    */
/*  一続きに行う。戻らない。                                                 */
/* ======================================================================== */
extern void ring3_resume(const u32 *frame, u32 pd_phys, void *tss);

/* ======================================================================== */
/*  exec_launch — 外部プログラムのロードと実行 (exec_run / exec_start の実体) */
/*                                                                          */
/*  gui = 0: 従来の exec_run。子が終わるまで呼び出し元を塞ぐ。               */
/*  gui = 1: K5b の exec_start。子が最初の OP_WAIT で park した時点でも戻る。 */
/*                                                                          */
/*  違いは **どこで longjmp を受けるか** の 1 点だけで、ロードもレイアウトも  */
/*  共通。仮想レイアウト (app.ld / memmap.h の RING3_*) は 1 バイトも         */
/*  動かない (I12/I13) — 動いたのは「物理をどこから取るか」だけ。            */
/* ======================================================================== */
static int exec_launch(const char *cmdline, int gui_arg)
{
    /* longjmp の復帰側で読む唯一のローカル。volatile でフレーム上に固定する
     * — レジスタに置かれると longjmp で失われる (他は全部グローバルで判断)。 */
    volatile int gui = gui_arg;
    u32 load_base;
    u32 max_size;
    u32 stack_top;
    u32 guard_a, guard_b;
    u32 exec_heap_base, exec_heap_size;
    u32 sbrk_end;            /* 実際に物理を張る sbrk の上端 (= sbrk 上限) */
    int sbrk_tier = 0;       /* sbrk 物理の段 (1 = 従来式 / 2 = 最低分、0 = 非CPL3) */
    u32 stack_size = MEM_EXEC_STACK_SIZE;
    u32 legacy_pdes = 1;
    int is_shell;
    int launcher_id;
    int id;
    u32 need_pages;

    u32 mem_end = sys_usable_mem_end();
    u8 *load_addr;
    OS32Header *hdr;
    int sz;
    u32 code_off, text_sz, bss_sz, heap_sz, entry_off;
    ExecEntry entry;
    AppSlot *ctx;
    int want_ring3 = 0;      /* CPL=3 で走らせるか (OS32X_FLAG_RING3, v2 M1) */

    char path[VFS_MAX_PATH];
    /* 解決済みのパス。ヘッダを先に 1 ページ読むので、本体の読み込みでは
     * 同じ探索をやり直さずこちらを使う (探索でヒットした綴りを保つ)。 */
    static char resolved[VFS_MAX_PATH];
    /* ヘッダだけを先に読むカーネル側バッファ。本体を読む先の物理は、
     * ヘッダの text_size / bss_size / heap_size を見るまで決まらない
     * (D1 の「起動時の順序」手順 2)。 */
    static u8 hdrbuf[OS32X_HDR_SIZE];
    u32 launcher_pd = paging_current_cr3();
    int launch_cmd_len = 0, launch_read_failed = 0;
    while (launch_read_byte(launcher_pd, cmdline + launch_cmd_len, &launch_read_failed)) {
        if ((u32)launch_cmd_len == 0x7fffffffUL) return EXEC_ERR_INVALID;
        launch_cmd_len++;
    }
    if (launch_read_failed) {
        shell_print("Error: unreadable launch command\n", ATTR_RED);
        return EXEC_ERR_INVALID;
    }
    const char *p = cmdline;
    int i = 0;

    launcher_id = appslot_cur();
    is_shell = (exec_nest_level == 0);
    if (is_shell) g_layout_reject = 0;   /* 0 段の起動ごとに消す (子では触らない) */

    /* ---- ID の池 (D3)。物理の勘定はヘッダを読んでから ---- */
    if (is_shell) {
        id = APP_ID_SHELL;
    } else {
        id = appslot_start_admit(gui, 0, 0);
        if (id < 0) {
            if (id == OS32_ERR_FULL)
                shell_print("Error: too many programs running\n", ATTR_RED);
            return id;
        }
    }

    /* コマンドラインからパスを抽出。**切り詰めない** (票 TASK_VFS_FD_PATH
     * v3 の追記): 以前は 255 バイトで切って、切った名前の別のプログラムを
     * 起動し得た。収まらなければ「見つからない」と同じ戻り方で断る。 */
    while (*p == ' ') p++;
    while (*p && *p != ' ' && i < (int)sizeof(path) - 1) {
        path[i++] = *p++;
    }
    path[i] = '\0';
    if (*p && *p != ' ') {
        shell_print("Error: command name too long\n", ATTR_RED);
        return EXEC_ERR_NOT_FOUND;
    }

    /* ====== Level に応じたメモリレイアウト決定 ====== */
    if (is_shell) {
        /* シェル: 常駐帯域 0x300000-0x37FFFF */
        load_base = MEM_SHELL_LOAD_ADDR;
        max_size  = MEM_SHELL_MAX_SIZE;
        stack_top = MEM_SHELL_STACK_TOP;
        guard_a   = 0; /* シェルは sbrk/exec_heap 未使用 */
        guard_b   = MEM_SHELL_GUARD;
        sbrk_end  = 0;
        exec_heap_base = 0;
        exec_heap_size = 0;
    } else {
        load_base = MEM_EXEC_LOAD_ADDR;
        stack_top = MEM_APP_STACK_TOP;
        guard_a = guard_b = sbrk_end = exec_heap_base = exec_heap_size = 0;
        max_size = MEM_EXEC_HEAP_BASE - load_base;

    }

    load_addr = (u8 *)load_base;

    /* ====== ヘッダだけ先読み (master CR3、カーネルバッファ) ======
     * 現行のように全部読んでから枚数を決めることはできない — 読む先の
     * 物理がまだ無いため (D1 の手順 2)。 */
    kstrncpy(resolved, path, VFS_MAX_PATH);
    sz = vfs_read(resolved, hdrbuf, (int)sizeof(hdrbuf));

    /* フォールバック: パスにスラッシュがない場合、標準ディレクトリを順に検索 */
    /* 注意: SYS_DEFAULT_PATH (config.h) と整合させること */
    if (sz <= 0) {
        int has_slash = 0;
        int pi;
        for (pi = 0; path[pi]; pi++) {
            if (path[pi] == '/') { has_slash = 1; break; }
        }
        if (!has_slash) {
            static const char *search_dirs[] = {
                "/bin/", "/sbin/", "/usr/bin/", (const char *)0
            };
            int di;
            for (di = 0; search_dirs[di]; di++) {
                /* 連結が溢れる候補は切り詰めて試さない (別の名前になる) */
                if (kstrlen(search_dirs[di]) + kstrlen(path) + 1 > VFS_MAX_PATH)
                    continue;
                kstrncpy(resolved, search_dirs[di], VFS_MAX_PATH);
                kstrncat(resolved, path, VFS_MAX_PATH);
                sz = vfs_read(resolved, hdrbuf, (int)sizeof(hdrbuf));
                if (sz > 0) break;
            }
        }
    }

    if (sz <= 0) {
        return EXEC_ERR_NOT_FOUND;
    }

    hdr = (OS32Header *)hdrbuf;

    /* ---- KAPI データ欄の配置の照合 (票 TASK_KAPI_DATA_FIELDS、ヘッダ v3) ----
     * v62 以前のバイナリはデータ欄 (sbrk_heap_limit / shm_base) を別の
     * オフセットで読む。走らせると malloc が全部 ENOMEM になったり共有メモリの
     * 番地を取り違えたりするので、**常駐シェルも含めて**ここで断る
     * (シェルの停止と案内は kernel.c が exec_layout_rejected() を見て出す)。 */
    {
        int lrc = os32x_layout_check(hdr, (u32)sz, (u32)KAPI_DATA_FIELDS_OFF);
        if (lrc != OS32X_LAYOUT_OK) {
            if (is_shell) g_layout_reject = 1;
            kprintf(0xC1, "[exec] %s: %s (bin=%x kernel=%x)\n", path,
                    os32x_layout_reason(lrc),
                    (lrc == OS32X_LAYOUT_MISMATCH) ? hdr->kapi_data_off : 0u,
                    (u32)KAPI_DATA_FIELDS_OFF);
            const char *reason = os32x_layout_reason(lrc);
            if (lrc == OS32X_LAYOUT_MISMATCH) {
                if (hdr->kapi_data_off != KAPI_DATA_FIELDS_OFF) reason = "KAPI data layout mismatch";
                else if (hdr->kapi_abi_generation != OS32_KAPI_ABI_GENERATION) reason = "KAPI ABI generation mismatch";
                else if (hdr->memory_layout_generation != OS32_MEMORY_LAYOUT_GENERATION) reason = "memory layout generation mismatch";
                else if (hdr->min_api_ver > KAPI_VERSION) reason = "required KAPI version is newer than kernel";
                else reason = "shared library protocol mismatch";
            }
            shell_print(reason, ATTR_RED);
            shell_print("; rebuild required\n", ATTR_RED);
            return EXEC_ERR_INVALID;
        }
    }

    {
        const char *reason = exec_image_reject_reason(hdr, is_shell, load_base, max_size);
        if (reason) {
            if (is_shell) g_layout_reject = 1;
            shell_print(reason, ATTR_RED);
            return EXEC_ERR_INVALID;
        }
    }
    code_off  = hdr->header_size;
    text_sz   = hdr->text_size;
    bss_sz    = hdr->bss_size;
    heap_sz   = hdr->heap_size;
    entry_off = hdr->entry_offset;

    /* ---- CUI 専用の宣言 (票 T8-2、受入 F5 の不合格を受けて) ----
     * `--cpl0` の砦 (下) は CPL=0 のプログラムしか捕まえない。v86.bin は
     * flags 0x0 の **CPL=3** プログラムで、V86 へは KAPI (v86_*) を通して
     * カーネル側から入るので素通りしていた。OS32X_FLAG_CUI_ONLY
     * (mkos32x --cui-only / app.conf の 4 列目 `cui`) を見て、GUI からの
     * 起動 (exec_start) だけをここで断つ。CUI の exec_run は無変更。
     * 帯も池もまだ 1 つも動かしていない位置に置くこと。 */
    if (!is_shell && appslot_cui_only_admit(gui, hdr->flags) < 0) {
        shell_print("Error: cui only - run this from CUI mode\n", ATTR_RED);
        return OS32_ERR_INVAL;
    }

    want_ring3 = !is_shell;
    if (want_ring3) {
        if (exec_stack_bytes(hdr->stack_size, (u32)launch_cmd_len, &stack_size))
            return EXEC_ERR_INVALID;
        guard_b = stack_top - stack_size - PAGE_SIZE;
    }

    if (!is_shell) {
        /* 子プロセス帯のレイアウト確定 (include/memmap.h 参照):
         *   [load..code_end) 本体 / [code_end..guard_a) sbrk / [guard_a] ガード /
         *   [exec_heap_base..heap_top) exec_heap */
        u32 code_end = PAGE_ALIGN_UP(MEM_PHYS_EXEC_FLOOR + text_sz + bss_sz);
        u32 heap_top, avail, old_exec, old_guard, old_sbrk;
        legacy_pdes = paging_app_band_pdes(code_end, heap_sz, mem_end);
        heap_top = MEM_LEGACY_APP_BASE + legacy_pdes * MEM_APP_BAND_PDE_SIZE;
        if (mem_end < heap_top) heap_top = mem_end & ~(u32)(PAGE_SIZE - 1);
        if (heap_top < MEM_EXEC_STACK_SIZE + PAGE_SIZE) return EXEC_ERR_NOMEM;
        heap_top -= MEM_EXEC_STACK_SIZE + PAGE_SIZE;
        if (heap_top < code_end || heap_top - code_end < MEM_EXEC_SBRK_MIN + PAGE_SIZE + MEM_EXEC_HEAP_MIN)
            return EXEC_ERR_NOMEM;
        avail = heap_top - code_end - MEM_EXEC_SBRK_MIN - PAGE_SIZE;
        if (heap_sz) {
            if (heap_sz > 0xffffffffUL - (PAGE_SIZE - 1)) return EXEC_ERR_INVALID;
            exec_heap_size = PAGE_ALIGN_UP(heap_sz);
            if (exec_heap_size < MEM_EXEC_HEAP_MIN) exec_heap_size = MEM_EXEC_HEAP_MIN;
            if (exec_heap_size > avail) return EXEC_ERR_NOMEM;
        } else {
            exec_heap_size = (avail / 2) & ~(u32)(PAGE_SIZE - 1);
            if (exec_heap_size < MEM_EXEC_HEAP_MIN) exec_heap_size = MEM_EXEC_HEAP_MIN;
        }
        old_exec = heap_top - exec_heap_size;
        old_guard = old_exec - PAGE_SIZE;
        if (heap_sz) { old_sbrk = code_end + MEM_EXEC_SBRK_MIN; sbrk_tier = 2; }
        else sbrk_tier = exec_sbrk_pick_tier(MEM_PHYS_EXEC_FLOOR, code_end, old_guard,
                                           exec_heap_size, stack_size, pgalloc_free_pages(), &old_sbrk);
        if (!sbrk_tier) return EXEC_ERR_NOMEM;
        sbrk_end = MEM_EXEC_LOAD_ADDR + (old_sbrk - MEM_PHYS_EXEC_FLOOR);
        exec_heap_base = MEM_EXEC_HEAP_BASE;
        guard_a = sbrk_end;
        if (sbrk_end + PAGE_SIZE > exec_heap_base || exec_heap_size > guard_b - exec_heap_base)
            return EXEC_ERR_NOMEM;
    } else if (text_sz + bss_sz > max_size) {
        shell_print("[DBG] NOMEM: text=", 0xE1);
        shell_print_dec(text_sz, 0xE1);
        shell_print(" bss=", 0xE1);
        shell_print_dec(bss_sz, 0xE1);
        shell_print(" max=", 0xE1);
        shell_print_dec(max_size, 0xE1);
        shell_print("\n", 0xE1);
        return EXEC_ERR_NOMEM;
    }

    /* ======== 物理の勘定 (D5)。入らなければ拒否、切り詰めない ======== */
    need_pages = 0;
    if (want_ring3) {
        need_pages = exec_ring3_pages(load_base, sbrk_end, exec_heap_size, stack_size);
        if (appslot_start_admit(gui, need_pages, pgalloc_free_pages()) < 0) {
            shell_print("[DBG] NOMEM: need pages=", 0xE1);
            shell_print_dec(need_pages, 0xE1);
            shell_print(" free=", 0xE1);
            shell_print_dec(pgalloc_free_pages(), 0xE1);
            shell_print("\n", 0xE1);
            exec_restore_band(launcher_id);
            return EXEC_ERR_NOMEM;
        }
    }

    /* ======== 起動元のヒープ使用量を控える (I7) ========
     * ここから先の失敗 (exec_launch_abort) は起動元の状態を戻すので、
     * **失敗しうる操作より前に**控えておく。後ろに置くと、巻き戻しが
     * 古い exec_heap_used で起動元のヒープ管理変数を上書きする。 */
    if (!is_shell) {
        exec_heap_save_state(&appslot_at(launcher_id)->exec_heap_used);
    }

    /* ======== スロットに諸元を書く (まだ commit しない) ======== */
    ctx = appslot_at(id);
    ctx->load_addr = load_base;
    ctx->stack_top = stack_top;
    ctx->stack_size = stack_size;
    ctx->stack_base = stack_top - stack_size;
    ctx->guard_a = guard_a;
    ctx->guard_b = guard_b;
    ctx->exec_heap_base = exec_heap_base;
    ctx->exec_heap_size = exec_heap_size;
    ctx->exec_heap_used = 0;
    ctx->sbrk_heap_limit = is_shell ? guard_b : sbrk_end;
    ctx->cpl3 = 0;
    /* 票 T8 D1a: 全画面 GFX の宣言 (OS32X_FLAG_GFX) を見るのは gfx_init を
     * 呼ばれた瞬間なので、起動時にヘッダの flags を控えておく。 */
    ctx->hdr_flags = hdr->flags;
    ctx->band_top = g_ring3_band_top;
    ctx->band_pdes = g_ring3_band_pdes;
    ctx->pages = need_pages;

    /* ======== CPL=3: アドレス空間と per-app 物理 (D1) ======== */
    if (want_ring3) {
        u32 as_owner;

        /* AS owner は AS 作成の直前に取り、struct addrspace に持つ
         * (TASK_T1_LEDGER §4-8)。返却は exec_teardown_app。 */
        if (!ledger_owner_new(LEDGER_KIND_AS, (u32)id, "app", &as_owner)) {
            shell_print("Error: ring3 addrspace create failed\n", ATTR_RED);
            exec_restore_band(launcher_id);
            return EXEC_ERR_NOMEM;
        }
        paging_load_cr3(paging_kernel_pd_phys());
        ctx->as = kmalloc(sizeof(*ctx->as));
        if (!ctx->as || paging_addrspace_create_lease(ctx->as, as_owner) != 0) {
            if (ctx->as) kfree(ctx->as);
            ctx->as = 0;
            (void)ledger_owner_retire(as_owner);
            exec_restore_context(launcher_id);
            return EXEC_ERR_NOMEM;
        }
        ctx->cpl3 = 1;

        /* **I6**: アプリ PT は master の identity PTE で初期化されている。
         * 落とし忘れると物理 0x5xxxxx が素通しで見え、他アプリのページや
         * pgalloc の作業域が CPL=3 から読める。ここが本設計で最も静かに
         * 壊れる箇所なので、per-app 物理を張る前に必ず全部 0 にする。 */
        paging_addrspace_clear_app_band(ctx->as);

        /* 3 領域を per-app 物理で張る。連続が取れなければページ単位へ倒す
         * (D10 の断片化の申し送り)。ガードは張らない = 非 present のまま。 */
        if (app_map_region(ctx->as, load_base, sbrk_end) != 0 ||
            app_map_region(ctx->as, exec_heap_base,
                           exec_heap_base + exec_heap_size) != 0 ||
            app_map_region(ctx->as, ctx->stack_base, ctx->stack_top) != 0) {
            shell_print("Error: out of physical memory for app\n", ATTR_RED);
            return exec_launch_abort(launcher_id, id, EXEC_ERR_NOMEM);
        }

        /* 3 領域を張り終えてから段を記録する (途中で失敗したものは数えない)。*/
        if (sbrk_tier == 1 || sbrk_tier == 2) {
            exec_sbrk_tier_last = (u32)sbrk_tier;
            exec_sbrk_tier_count[sbrk_tier - 1]++;
        }

        /* VRAM (テキスト 0xA0000 + グラフィック 0xA8000) — C2: 全PD共有+USER */
        paging_addrspace_map_user_range(ctx->as,
            0xA0000UL, 0xC0000UL, PAGE_RW | PTE_USER);
        /* SHM (アプリ間データ受け渡し) — C2: 全PD共有+USER */
        paging_addrspace_map_user_range(ctx->as,
            (u32)MEM_SHM_BASE, (u32)MEM_SHM_BASE + (u32)MEM_SHM_SIZE,
            PAGE_RW | PTE_USER);
        /* フォントキャッシュ (0x01000-0x49FFF): kcg フォントビットマップ直読 */
        paging_addrspace_map_user_range(ctx->as,
            (u32)MEM_FONT_CACHE_BASE, (u32)MEM_UNICODE_TABLE_BASE,
            PAGE_RW | PTE_USER);
        /* Unicode-JIS 変換表 (0x4A000, 128KB): unicode_to_jis() 直読 */
        paging_addrspace_map_user_range(ctx->as,
            (u32)MEM_UNICODE_TABLE_BASE,
            (u32)MEM_UNICODE_TABLE_BASE + (u32)MEM_UNICODE_TABLE_SIZE,
            PAGE_RW | PTE_USER);
        /* 9801 の主記憶バックバッファ (0x6A000, 128KB) は **常に** USER に
         * する (レビュー #6)。Cirrus の setup 失敗で 9801 へ落ちたとき、
         * 最初の CPU 描画が #PF になるのを防ぐ。 */
        paging_addrspace_map_user_range(ctx->as,
            (u32)MEM_GFX_BB_BASE,
            (u32)MEM_GFX_BB_BASE + (u32)MEM_GFX_BB_SIZE,
            PAGE_RW | PTE_USER);
        /* いま選ばれているバックエンド固有の面 (gfx_bb_phys_range) を恒等の
         * まま丸ごと足す (exec_map_shared_bb の注釈)。私有領域と重なるなら
         * 起動を断る — 重ねて写すと私有ページの PTE が消える。 */
        if (exec_map_shared_bb(ctx->as, ctx->band_top) != 0) {
            shell_print("Error: backbuffer overlaps app area\n", ATTR_RED);
            return exec_launch_abort(launcher_id, id, EXEC_ERR_NOMEM);
        }
        /* KAPI トランポリンページ (RO+USER, 全PD共有) */
        paging_addrspace_map_user(ctx->as, ring3_tramp_page,
            V2P((const void *)ring3_tramp_page), PAGE_RO | PTE_USER);

        /* --- K3: 共有ライブラリ帯域 (0x400000-0x4FFFFF) ---
         * .text/.rodata は RO+USER、.data/.bss は同じ仮想番地にこのアプリ
         * 専用の物理ページ (原本から複製)。**master CR3 のまま**行う —
         * 原本 g_data_master は共有ライブラリ帯の末尾 = アプリ固有 PDE の
         * 中にあり、アプリ CR3 の下では別物を指す (I11)。 */
        if (shlib_addrspace_attach(ctx->as) < 0) {
            shell_print("Error: shlib data attach failed (out of memory)\n", ATTR_RED);
            return exec_launch_abort(launcher_id, id, EXEC_ERR_NOMEM);
        }
    }

    /* ======== setjmp — この ID の呼び出し元へ帰る点 ======== */
    if (exec_setjmp(ctx->jmpbuf) != 0) {
        /* ======== longjmp復帰ポイント ========
         * ローカル変数は当てにできない (setjmp 後に書き換わったものが
         * 復帰側では読めない)。判断材料はグローバルだけに限る。
         *
         * フォルト経由の復帰では例外ゲートが IF をクリアしたまま longjmp
         * してくる (exec_longjmp は EFLAGS を復元しない)。呼び出し元は常に
         * 割り込み有効で動いているので、ここで無条件に開けてよい。 */
        exec_pending_finish();
        _enable();
        /* 畳み (終了 / fault / CTRL+STOP) も park も、戻す作業は
         * exec_exit / exec_pending_finish / exec_park で済んでいる。 */
        if (g_longjmp_reason == EXEC_LJ_PARK) {
            return g_longjmp_id;      /* app_id (2〜5) — まだ生きている */
        }
        return gui ? 0 : exec_exit_status;
    }

    entry = (ExecEntry)(load_addr + entry_off);

    /* Read directly into physical backing under the master PD. */
    {
        int fd = vfs_open(resolved, 0);
        u32 pos = 0;
        if (fd < 0) return exec_launch_abort(launcher_id, id, EXEC_ERR_NOT_FOUND);
        if (vfs_seek(fd, (int)code_off, 0) < 0) {
            vfs_close(fd);
            return exec_launch_abort(launcher_id, id, EXEC_ERR_INVALID);
        }
        while (pos < text_sz) {
            u32 pa, n = PAGE_SIZE - ((load_base + pos) & (PAGE_SIZE - 1));
            if (n > text_sz - pos) n = text_sz - pos;
            if (want_ring3) {
                if (as_va_to_pa(ctx->as->pd_phys, load_base + pos, &pa)) break;
            } else pa = load_base + pos;
            if (vfs_read_fd(fd, P2V(pa), n) != (int)n) break;
            pos += n;
        }
        vfs_close(fd);
        if (pos != text_sz) return exec_launch_abort(launcher_id, id, EXEC_ERR_INVALID);
    }
    /* app_map_region zeroes all pages, including BSS and padding. */
    if (!want_ring3) kmemset(load_addr + text_sz, 0, bss_sz);

    /* ヒープ・ガードページ設定 */
    if (is_shell) {
        /* シェルのヒープは 2 系統あり、領域を分ける (include/memmap.h 参照):
         *   - newlib の sbrk (malloc / stdio バッファ): BSS 終端 〜 guard_b
         *   - KAPI mem_alloc (exec_heap): スタック上の MEM_SHELL_HEAP_BASE 〜 */
        exec_heap_base = MEM_SHELL_HEAP_BASE;
        exec_heap_size = MEM_SHELL_HEAP_SIZE;
        if (heap_sz > 0 && heap_sz < exec_heap_size) exec_heap_size = heap_sz;
        ctx->exec_heap_base = exec_heap_base;
        ctx->exec_heap_size = exec_heap_size;
        kapi->sbrk_heap_limit = guard_b;
    }


    if (!is_shell) {
        kapi->sbrk_heap_limit = sbrk_end;

    }

    {
        u32 launch_argv[OS32_MAX_ARGS];
        u32 str_addr, argv_addr;
        u32 frame[4];
        int argc = 0;
        int cmd_len = launch_cmd_len;
        const char *s = cmdline;
        u32 d;
        int copy_failed = 0;
        u32 new_esp;
        u32 u_esp;   /* ring3: iret に渡すユーザ ESP (ダミー retaddr 込み) */
        /* 呼び出し元 ESP の退避先。ローカルにしないのは、子のスタックへ
         * 切り替えた後の復帰ムーブが %esp/%ebp 相対では読めないため。
         * ID 別に持つ (単一 static だとネスト exec で上書きされる)。 */
        static u32 saved_esp_stack[APP_SLOT_COUNT];

        stack_top -= (cmd_len + 1);
        stack_top &= ~((u32)STACK_ALIGN_MASK);
        str_addr = stack_top;

        stack_top -= sizeof(char *) * OS32_MAX_ARGS;
        argv_addr = stack_top;

        s = cmdline;
        d = str_addr;
        while (launch_read_byte(launcher_pd, s, &copy_failed)) {
            char quote;

            /* 引数間の空白をスキップ */
            while (launch_read_byte(launcher_pd, s, &copy_failed) == ' ') s++;
            if (!launch_read_byte(launcher_pd, s, &copy_failed)) break;

            /* 新しい引数を開始 */
            if (argc < OS32_MAX_ARGS - 1) launch_argv[argc++] = d;

            /* クォート対応トークナイザ */
            while (launch_read_byte(launcher_pd, s, &copy_failed) && launch_read_byte(launcher_pd, s, &copy_failed) != ' ') {
                if (launch_read_byte(launcher_pd, s, &copy_failed) == '"' || launch_read_byte(launcher_pd, s, &copy_failed) == '\'') {
                    quote = launch_read_byte(launcher_pd, s++, &copy_failed);
                    while (launch_read_byte(launcher_pd, s, &copy_failed) && launch_read_byte(launcher_pd, s, &copy_failed) != quote) {
                        if (launch_read_byte(launcher_pd, s, &copy_failed) == '\\' && quote == '"' && launch_read_byte(launcher_pd, s + 1, &copy_failed)) {
                            s++;
                        }
                        { u8 ch = launch_read_byte(launcher_pd, s++, &copy_failed); copy_failed |= app_store(ctx, d++, &ch, 1); }
                    }
                    if (launch_read_byte(launcher_pd, s, &copy_failed) == quote) s++;  /* 閉じクォートをスキップ */
                } else if (launch_read_byte(launcher_pd, s, &copy_failed) == '\\' && launch_read_byte(launcher_pd, s + 1, &copy_failed)) {
                    s++;
                    { u8 ch = launch_read_byte(launcher_pd, s++, &copy_failed); copy_failed |= app_store(ctx, d++, &ch, 1); }
                } else {
                    { u8 ch = launch_read_byte(launcher_pd, s++, &copy_failed); copy_failed |= app_store(ctx, d++, &ch, 1); }
                }
            }
            { u8 ch = 0; copy_failed |= app_store(ctx, d++, &ch, 1); }
        }
        launch_argv[argc] = 0;
        if (copy_failed || app_store(ctx, argv_addr, launch_argv, (argc + 1) * sizeof(u32)))
            return exec_launch_abort(launcher_id, id, EXEC_ERR_INVALID);

        /* ---- 呼び出しフレームを子スタック上に自分で組む ----
         * ExecEntry は __cdecl (int argc, char **argv, KernelAPI *api) なので
         * 低位から argc, argv, kapi の順に並べる。call 時点で ESP を 16 バイト
         * 境界に揃えるのは SysV i386 ABI の要求。 */
        new_esp = (stack_top - 3 * sizeof(u32)) & ~(u32)15;
        frame[1] = (u32)argc;
        frame[2] = argv_addr;
        frame[3] = (u32)kapi;

        if (want_ring3) {
            /* --- M2c: CPL=3 アプリには本物の表でなくトランポリン表を渡す ---
             * CPL=3 の sbrk 上限は guard_a (exec_heap の直下のガード)。 */
            ((u32 *)ring3_tramp_page)[KAPI_DATA_IDX_SBRK_HEAP_LIMIT] = sbrk_end;
            ((u32 *)ring3_tramp_page)[KAPI_DATA_IDX_SHM_BASE] = kapi->shm_base;
            frame[3] = ring3_tramp_page;   /* api = トランポリン */

            /* --- crt0 スタック規約合わせ (retaddr ズレ修正) ---
             * ring3 は iret でエントリへ飛ぶため call が無く retaddr が
             * 積まれない。iret に渡す ESP を argc の 1 スロット下にし、
             * そこにダミー retaddr を置いて call 経路と同一に揃える。 */
            u_esp = new_esp - sizeof(u32);
            frame[0] = 0;   /* ダミー retaddr */

            g_cur_app = ctx;
        }

        if (app_store(ctx, want_ring3 ? u_esp : new_esp,
                      want_ring3 ? frame : frame + 1,
                      (want_ring3 ? 4 : 3) * sizeof(u32)))
            return exec_launch_abort(launcher_id, id, EXEC_ERR_INVALID);
        /* The heap header needs its user VA; switch only after image and argv. */
        if (want_ring3) paging_load_cr3(ctx->as->pd_phys);
        if (exec_heap_size) exec_heap_init_at(exec_heap_base, exec_heap_size);

        /* ======== ここで初めて「走っているのはこの ID」になる ======== */
        if (is_shell) {
            appslot_shell_commit();
        } else {
            appslot_start_commit(id, gui, need_pages);
            /* 暴走判定の起点 (票 T9 §12 S6)。 */
            appslot_mark_scheduled(id, tick_count);
        }
        exec_nest_level = ctx->depth;

        if (want_ring3) {
            /* --- M1d: CPL=3 に降りる ---
             * cli → TSS.ESP0 に現在のカーネル ESP → CR3 をアプリ PD へ →
             * セグメント → iret (IF=1 はユーザモードに入ると同時)、を 1 つの
             * 原始命令で行う。途中に割り込みが入ると TSS.ESP0 と CR3 が
             * 食い違うので分けない (契約は include/cpu.h、x86 の命令列は
             * arch/x86/arch_cpu.h)。CPL=3 実行中の割り込み / int 0x80 の
             * フレームはこの直下に積まれ、setjmp フレームを踏まない。
             * ここから通常 return しない — 終了は int 0x80 → longjmp。 */
            exec_entry_calls++;
            arch_enter_user(ctx->as->pd_phys, u_esp, (u32)entry);
            /* iret 後はここへ戻らない */
        } else {
            exec_entry_calls++;
            arch_call_on_stack(saved_esp_stack[id], new_esp, entry);
        }
        /* CPL=0 プログラムから普通に戻ってきた = 正常終了 (§1 事実 15)。 */
        exec_exit(EXEC_SUCCESS, EXEC_KIND_EXITED);
    }

    return EXEC_SUCCESS;
}

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

/* ======================================================================== */
/*  exec_park — 走っているアプリを OP_WAIT の中で止め、WM へ戻す (KAPI v44)  */
/*                                                                          */
/*  成立すれば **戻らない** (longjmp で exec_start / exec_resume の復帰点へ)。*/
/*  呼べない文脈 (OP_WAIT 以外の op / 走っているアプリが居ない / CPL=0 の子) */
/*  では OS32_ERR_INVAL を返して普通に戻り、ring3_park_reject_count が増える。*/
/*                                                                          */
/*  park 規約 (D2): 呼んでよいのは gshell の op_wait のループ先頭、wm_cycle  */
/*  が 1 周を終えた直後・ring::pending を読む前だけ。ここから longjmp する    */
/*  ので、WM が書きかけの状態を持っていると宙に浮く。                        */
/* ======================================================================== */
i32 exec_park(void)
{
    int id = appslot_cur();
    AppSlot *a;
    u32 k;
    int rc;

    rc = appslot_park_check();
    if (rc < 0) return rc;

    a = appslot_get(id);
    /* CPL=3 のフレームが無ければ止めようがない (CPL=0 の子 / 呼び出し文脈が
     * syscall の外)。check を通っていても最後にここで弾く。 */
    if (!a || !g_cur_app || g_cur_app != a || g_cur_frame == 0) {
        ring3_park_reject_count++;
        return OS32_ERR_INVAL;
    }

    /* CPL=3 の続き = int80_stub のフレーム 13 語。resume はこれを積み直して
     * popad; iretd するだけ (D2 の (b): 追加 RAM は 1 アプリ 52B)。 */
    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_in_syscall = 0;       /* この syscall はここで終わる */
    ring3_wm_depth = 0;         /* OP_WAIT の中から longjmp する — 出口を通らない */
    g_cur_frame = 0;

    /* master へ戻してから状態を切り替える (WM は master の下で走る)。 */
    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_commit();      /* PARKED + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_park_kbd — GUI 中の kbd 待ちで止める (第 2 の park 点、票 K7 D1)    */
/*                                                                          */
/*  drivers/kbd.c の kbd_getchar / kbd_getkey が、GUI モードで注入リングが    */
/*  空のときに呼ぶ。成立すれば **戻らない** (exec_park と同じ longjmp)。      */
/*  起こすのは WM で、そのとき exec_resume が注入リングの 1 バイトを EAX へ   */
/*  入れるので、アプリからは kbd_getchar() が普通に値を返したように見える。   */
/*                                                                          */
/*  戻り値 0 = park できなかった。呼び手は従来の `hlt` 待ちへ落ちる。         */
/*  「できなかった」の大半は CPL=0 の呼び手 (常駐シェル) や syscall の外で、  */
/*  これは異常ではないので数えない (票 §5 R1 の条件)。数えるのは CPL=3 の     */
/*  フレームを持ちながら表の側で弾かれた場合だけ                             */
/*  (= CUI の入れ子 exec_run の子。ring3_park_reject_count)。                */
/* ======================================================================== */
int exec_park_kbd(void)
{
    int id;
    AppSlot *a;
    u32 k;

    /* 票 §5 R1: CPL=3 のアプリが syscall の中に居るときだけ止められる。 */
    if (!g_cur_app || !g_cur_app->cpl3 || g_cur_frame == 0) return 0;

    id = appslot_cur();
    if (appslot_park_kbd_check() < 0) return 0;

    a = appslot_get(id);
    if (!a || g_cur_app != a) {
        ring3_park_reject_count++;
        return 0;
    }

    /* 以降は exec_park と 1 行も変えない (D2 の (b): フレーム 13 語を写して
     * master へ戻り、WM の待っている復帰点へ longjmp する)。 */
    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_in_syscall = 0;       /* この syscall はここで終わる */
    ring3_wm_depth = 0;         /* OP_WAIT の中から longjmp する — 出口を通らない */
    g_cur_frame = 0;

    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_kbd_commit();  /* WAIT_KEY + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_park_poll — ポーリング型の協調 yield (第 3 の park 点、票 T8 §7 D8) */
/*                                                                          */
/*  drivers/kbd.c の kbd_trygetchar / kbd_trygetkey が、GUI モードで注入     */
/*  リングが空のときに呼ぶ。**1 周だけ** WM へ譲る park で、手順は           */
/*  exec_park_kbd と 1 行も違わない (フレーム 13 語を写して master へ戻り、   */
/*  WM の待っている復帰点へ longjmp する)。違うのは 2 つだけ:                */
/*                                                                          */
/*    - 印 / 状態が parked_from_poll / WAIT_POLL であること                  */
/*    - **PIT tick の間引き** (10ms に 1 回まで) が掛かること。描画ループの   */
/*      busy-wait から秒間数万回来るので、間引きが無いと譲りだけで CPU を    */
/*      食う。tick は呼び手 (drivers/kbd.c が tick_count を読む) から渡す。   */
/*                                                                          */
/*  起こすのは WM で、そのとき exec_resume は注入リングに文字があればそれを、 */
/*  空なら **-1 (キーなし)** を EAX に入れる。アプリからは                    */
/*  kbd_trygetchar() が普通に戻ったように見える。                            */
/*                                                                          */
/*  戻り値 0 = 譲れなかった。呼び手はそのまま -1 を返す (従来どおり)。        */
/*  「譲れなかった」の大半は間引きと CPL=0 の呼び手で、どちらも異常では      */
/*  ないので数えない (数えるのは appslot 側の表の検査で弾かれた分だけ)。      */
/* ======================================================================== */
int exec_park_poll(u32 now_tick)
{
    int id;
    AppSlot *a;
    u32 k;

    /* R1 (exec_park_kbd と同じ): CPL=3 のアプリが syscall の中に居るときだけ。
     * ここは数えない — 間引きより前に置いて、CPL=0 の呼び手 (常駐シェル) が
     * tick の枠を食わないようにする。 */
    if (!g_cur_app || !g_cur_app->cpl3 || g_cur_frame == 0) return 0;

    id = appslot_cur();
    /* 間引き (tick) → 表の検査。順番は appslot_park_poll_check の中で固定。 */
    if (appslot_park_poll_check(now_tick) < 0) return 0;

    a = appslot_get(id);
    if (!a || g_cur_app != a) {
        ring3_park_reject_count++;
        return 0;
    }

    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_in_syscall = 0;       /* この syscall はここで終わる */
    ring3_wm_depth = 0;         /* OP_WAIT の中から longjmp する — 出口を通らない */
    g_cur_frame = 0;

    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_poll_commit();          /* WAIT_POLL + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_sys_yield — 明示的な譲り (第 4 の park 点、KAPI v49 sys_yield、D5)  */
/*                                                                          */
/*  「いま譲る」と書いた呼び手 (sh の sh_launch が launch_poll の合間に 1 回  */
/*  ずつ呼ぶ) のための park 点。exec_park_poll と手順は 1 行も違わないが、    */
/*  違うのは 2 つ:                                                          */
/*                                                                          */
/*    - **PIT tick の間引きを掛けない**。間引きは描画ループの busy-wait 用で、*/
/*      明示的な譲りに掛けると、同じ tick の中で sh が回り続けて子が走れない。*/
/*    - 印が parked_from_yield で、起こすとき exec_resume は **注入リングを   */
/*      読まず** EAX = 0 を入れる。読むと、sh が譲っている間に届いた子宛の    */
/*      1 バイトを sh が吸って捨てる (票 §6 blocker 1)。                     */
/*                                                                          */
/*  状態は WAIT_POLL のまま (WM から見た起こし方の規則を増やさない)。        */
/*  GUI 中は **必ず** park する (tick 制限なし)。park できない文脈            */
/*  (CUI / CPL=0 / syscall の外 / 入れ子 exec_run の子) では `hlt` 1 回して    */
/*  0 で戻る — 呼び手から見れば「譲った」で同じ。                             */
/* ======================================================================== */
i32 exec_sys_yield(void)
{
    int id;
    AppSlot *a;
    u32 k;

    /* CUI 中は協調型の相手 (WM) が居ない。従来どおり 1 回 hlt して戻る。 */
    if (!con_sink_is_enabled()) { _halt(); return 0; }

    /* R1 (exec_park_kbd / exec_park_poll と同じ): CPL=3 のアプリが syscall の
     * 中に居るときだけ。CPL=0 の呼び手はここで落ちる。 */
    if (!g_cur_app || !g_cur_app->cpl3 || g_cur_frame == 0) { _halt(); return 0; }

    id = appslot_cur();
    if (appslot_park_yield_check() < 0) { _halt(); return 0; }

    a = appslot_get(id);
    if (!a || g_cur_app != a) {
        ring3_park_reject_count++;
        _halt();
        return 0;
    }

    for (k = 0; k < APP_FRAME_WORDS; k++) a->frame[k] = g_cur_frame[k];

    exec_heap_save_state(&a->exec_heap_used);
    ring3_in_syscall = 0;       /* この syscall はここで終わる */
    ring3_wm_depth = 0;         /* OP_WAIT の中から longjmp する — 出口を通らない */
    g_cur_frame = 0;

    paging_load_cr3(paging_kernel_pd_phys());
    appslot_park_yield_commit();         /* WAIT_POLL + 印 + owner 1 へ */
    exec_restore_context(APP_ID_SHELL);

    g_longjmp_reason = EXEC_LJ_PARK;
    g_longjmp_id = id;
    exec_longjmp(a->jmpbuf);    /* 戻らない */
    return 0;
}

/* ======================================================================== */
/*  exec_resume — 止めてあるアプリを 1 本だけ起こす (KAPI v44)               */
/*                                                                          */
/*  戻り値: app_id = また park した / 0 = 終了した / <0 = 起こせなかった      */
/*  wait_ret は OP_WAIT の戻り値 (契約 T3: ring::pending)。保存フレームの     */
/*  EAX スロットに書くので、アプリから見れば gui_call(OP_WAIT) が普通に        */
/*  その値を返したように見える。                                             */
/*                                                                          */
/*  起こせるのは **OP_WAIT で park された印のあるフレームだけ** (C5/C6)。     */
/*  印が無ければ OS32_ERR_STALE を返して ring3_resume_bad_frame_count を上げる*/
/*  — WM の行儀を信じるのではなくカーネルが弾く形 (受入 G7)。                 */
/* ======================================================================== */
i32 exec_resume(i32 app_id, i32 wait_ret)
{
    AppSlot *a;
    int rc;
    int src;
    u8 ch;

    rc = appslot_resume_check((int)app_id);
    if (rc < 0) return rc;

    a = appslot_get((int)app_id);
    if (!a->cpl3 || !a->as->pd_phys) return OS32_ERR_INVAL;
    src = appslot_resume_source((int)app_id);
    if (src == APP_RESUME_SRC_YIELD) {
        /* 票 T9 D5: 明示的な譲り (sys_yield) は **注入リングを読まない**。
         * 読むと、sh が譲っている間に届いた子宛の 1 バイトを吸って捨てる
         * (票 §6 blocker 1)。アプリからは sys_yield() が 0 を返して見える。 */
        a->frame[APP_FRAME_EAX] = 0;
    } else if (src == APP_RESUME_SRC_POLL) {
        /* 票 T8 §7 D8: ポーリング型は **1 周だけ**の譲りなので、注入リングが
         * 空でも起こす (WAIT_KEY と違って OS32_ERR_AGAIN を返さない)。
         * 空なら EAX = -1 = 「キーなし」で、アプリの kbd_trygetchar() は
         * 普通に -1 を返したように見える。WM が渡した wait_ret は使わない。 */
        ch = 0;
        if (kbd_inject_take(&ch)) a->frame[APP_FRAME_EAX] = (u32)ch;
        else                      a->frame[APP_FRAME_EAX] = (u32)(i32)-1;
    } else if (src == APP_RESUME_SRC_KBD) {
        /* 票 §5 の指摘 B: 文字の取り出しはここで完結する (WM 側に取り出し用
         * の KAPI は作らない)。WM が渡した wait_ret は**使わない**。
         * 空なら起こさず OS32_ERR_AGAIN — 印も状態も残るので、WM は次の周で
         * もう一度試せばよい (その周は譲る = streak に数えない)。 */
        ch = 0;
        if (!kbd_inject_take(&ch)) return OS32_ERR_AGAIN;
        a->frame[APP_FRAME_EAX] = (u32)ch;
    } else {
        a->frame[APP_FRAME_EAX] = (u32)wait_ret;
    }

    if (exec_setjmp(a->jmpbuf) != 0) {
        /* park / 終了 / fault / kill で戻ってきた。ローカルは当てにしない。 */
        exec_pending_finish();
        _enable();
        if (g_longjmp_reason == EXEC_LJ_PARK) return g_longjmp_id;
        return 0;
    }

    appslot_resume_commit((int)app_id);
    appslot_mark_scheduled((int)app_id, tick_count);   /* 票 T9 §12 S6 */
    exec_restore_context((int)app_id);
    /* cli → TSS.ESP0 → CR3 → popad; iretd を割り込み禁止で一続きに。
     * iretd が保存済み EFLAGS (IF=1) を復元するのでアプリ側の IF は変わらない。 */
    ring3_resume(a->frame, a->as->pd_phys, &kernel_tss);
    return 0;   /* 到達しない */
}

/* ======================================================================== */
/*  exec_kill — 止めてあるアプリを起こさずに畳む (KAPI v44、決裁 D9-6)       */
/*                                                                          */
/*  CTRL+STOP は「いま走っているアプリ」宛にしか立たない (IRQ1 の時点で       */
/*  カーネルが知っているのはそれだけ) ので、止めてあるアプリを畳む口が別に    */
/*  要る。これが無いと resume されないまま固まったアプリを永久に畳めない。    */
/*  owner 1 (WM top-level) からのみ。走っている本人には OS32_ERR_STALE。      */
/*                                                                          */
/*  票 T9 D8 以後、畳むのは **id とその子孫** (起動要求表の child を末尾まで   */
/*  辿ったもの) で、順番は **末尾から**。1 本分の手順が exec_kill_one。        */
/* ======================================================================== */
static void exec_kill_one(int id)
{
    AppSlot *a = appslot_get(id);
    if (!a) return;
    /* 走っていないので CR3 は master のまま。owner も 1 のまま動かさない
     * — 回収は全部 ID を明示して呼ぶ (D3)。 */
    exec_reclaim_resources(id);
    exec_teardown_app(a);
    appslot_reclaim(id);
    exec_notify_owned(id);
    /* 生存アプリの集合が変わる瞬間 = transition。G7 の switch ではない。 */
    ring3_transition_count++;
}

i32 exec_kill(i32 app_id)
{
    int chain[APP_MAX_APPS];
    int n;
    int i;
    int rc = appslot_kill_check((int)app_id);
    if (rc < 0) return rc;

    /* 票 T9 D8: 「id とその子孫を末尾から回収」に固定する。端末 → sh → 子の
     * ように要求表が連鎖しているとき、途中の 1 本だけを畳むと残りが孤児に
     * なる (WM の forget と CANCEL の DONE も壊れる — 票 §9 blocker 2)。
     * 末尾から畳むのは、各段の回収通知が親の表を DONE + child = 0 に
     * するため — 先に親を畳むと、まだ生きている子が誰の表にも載らなくなる。*/
    n = launch_chain((int)app_id, chain, APP_MAX_APPS);
    if (n <= 0) { exec_kill_one((int)app_id); return 0; }
    for (i = n - 1; i >= 0; i--) {
        /* 末尾側が既に畳まれている / 走っている本人だった場合は飛ばす
         * (先頭 app_id は上で検査済み)。 */
        if (chain[i] != (int)app_id && appslot_kill_check(chain[i]) < 0) continue;
        exec_kill_one(chain[i]);
    }
    return 0;
}

/* ======================================================================== */
/*  exec_app_state — 0=空き / 1=走っている / 2=park 中 (KAPI v44、任意)      */
/* ======================================================================== */
i32 exec_app_state(i32 app_id)
{
    return appslot_state((int)app_id);
}

/* ======================================================================== */
/*  exec_abort_clear — CTRL+STOP の要求を降ろす (KAPI v45、決裁 A1)          */
/*                                                                          */
/*  IRQ1 は宛先を選べず「いま走っているアプリ」に立てるが、契約 T6 の宛先は  */
/*  フォーカス窓のアプリ。WM (owner 1) がフォーカス窓の owner を見て、       */
/*  走っている本人でなければこれで本人の要求を降ろし、フォーカス窓の ID を   */
/*  exec_kill で畳む。降ろさなければ本人が次の syscall で畳まれてしまう      */
/*  (= 意図しない 1 本が死ぬ)。要求以外の状態は何も動かさない。              */
/*  0 = 降ろした / 要求が無かった、OS32_ERR_INVAL = owner 1 以外。           */
/* ======================================================================== */
i32 exec_abort_clear(void)
{
    return appslot_abort_clear();
}
