/* ======================================================================== */
/*  EXEC.H — 簡易プログラムローダー                                         */
/*                                                                          */
/*  ext2上のフラットバイナリを拡張メモリにロードして実行する。               */
/*  外部プログラムはKernelAPI構造体を通じてカーネル関数を呼び出す。          */
/*                                                                          */
/*  呼び出し規約:                                                           */
/*    カーネル: System V i386 ABI                                            */
/*    外部プログラム: System V i386 ABI                                      */
/*    KernelAPIの関数ポインタ: __cdecl ラッパー経由                          */
/*    ExecEntry (外部プログラムのmain): __cdecl                             */
/*                                                                          */
/*  メモリ配置:                                                             */
/*    KAPI_ADDR : KernelAPI テーブル (関数ポインタ群)                        */
/*    0x400000  : プログラムロード領域 (最大1MB)                             */
/* ======================================================================== */

#ifndef __EXEC_H
#define __EXEC_H

/* KernelAPI構造体・OS32Header・基本型は共有ヘッダから取得 */
#include "os32_kapi_shared.h"
#include "memmap.h"

/* ネスト実行の上限は exec/appslot.h の ID の池 (APP_ID_MIN..APP_ID_MAX)。
 * 同時に生きられる非シェル ID は APP_MAX_APPS = 4 で、GUI アプリと CUI の
 * 入れ子 exec_run が **1 つの池を共有する** — 決裁 D9-2。
 * 旧 MAX_EXEC_NEST は K5b で参照が消えたため削除した (2026-09-11)。 */

/* プログラムのロード先 (固定) */
/* ======== API ======== */
void exec_init(void);

/* 従来の起動。子が終わるまで呼び出し元を塞ぐ (CUI の入れ子はこれ)。 */
int exec_run(const char *cmdline);

/* 直前の exec_run の結果を「種別 + 値」で返す (KAPI v55、票 TASK_EXIT_STATUS)。
 * *kind = EXEC_KIND_*、*code = 種別が EXITED なら子の終了コード。
 * 戻り値 0 = 記録あり / OS32_ERR_INVAL = 記録なし (*kind = NONE, *code = 0)。
 * **exec_run は全 return 点で記録を書く** ので、起動しなかった場合に前回の
 * 記録を読むことはない。GUI 経路 (exec_start / exec_resume) の子は書かない。 */
int exec_last_result(int *kind, int *code);

/* 台帳の owner (TASK_T1_LEDGER §4-8、T1b): いま走っている CPL=3 アプリの
 * AS owner、アプリの外 (カーネル・シェル・--cpl0 の子) なら kernel。
 * V86 バッキングの確保 (v86_mem_setup) に渡す。 */
u32 exec_ledger_owner(void);

/* ---- アプリ 4 本の同時実行 (KAPI v44、票 docs/tasks/gui/v13) ----
 * 詳細は TASK_K5_multiapp.md の D4 / D8。呼べるのは owner 1 (シェル帯) だけ。 */

/* 塞がない起動。>0 = app_id (最初の OP_WAIT で park した) / 0 = park より前に
 * 終了した / <0 = 起動しなかった (INVAL / FULL / NOMEM / NOT_FOUND / INVALID)。*/
i32 exec_start(const char *cmdline);

/* park してあるアプリを 1 本だけ起こす。wait_ret は OP_WAIT の戻り値。
 * app_id = また park した / 0 = 終了した / <0 = 起こせなかった。
 * 起こせるのは印のあるフレームだけ (OS32_ERR_STALE): OP_WAIT 由来なら
 * parked_from_wait、kbd 待ち (WAIT_KEY) 由来なら parked_from_kbd、
 * ポーリングの譲り (WAIT_POLL) 由来なら parked_from_poll。
 * kbd 待ちの側は wait_ret を**使わず**、注入リングの 1 バイトを EAX に
 * 入れる。リングが空なら起こさず OS32_ERR_AGAIN (票 K7 §5 の指摘 B)。
 * ポーリングの側も wait_ret を使わないが、空でも **EAX = -1 で起こす**
 * (1 周だけの譲りなので、次の周に必ず戻す。票 T8 §7 D8)。 */
i32 exec_resume(i32 app_id, i32 wait_ret);

/* 走っているアプリを OP_WAIT の中で止め、WM へ戻す。成立すれば **戻らない**。
 * 呼べない文脈では OS32_ERR_INVAL を返して普通に戻る。 */
i32 exec_park(void);

/* 第 2 の park 点 (票 K7 D1): GUI 中に kbd が空のとき、走っている CPL=3 の
 * アプリを WAIT_KEY で止めて WM へ戻す。drivers/kbd.c から呼ぶ。
 * 成立すれば **戻らない**。0 = 止められなかった (呼び手は hlt 待ちへ)。 */
int exec_park_kbd(void);

/* 第 3 の park 点 (票 T8 §7 D8): GUI 中に注入リングが空で、前回の譲りから
 * PIT tick が進んでいるとき、走っている CPL=3 アプリを WAIT_POLL で止めて
 * **1 周だけ** WM へ譲る。drivers/kbd.c の kbd_trygetchar / kbd_trygetkey
 * から、now_tick に tick_count を渡して呼ぶ。成立すれば **戻らない**。
 * 0 = 譲れなかった (呼び手はそのまま -1 を返す)。 */
int exec_park_poll(u32 now_tick);

/* 第 4 の park 点 (票 T9 D5、KAPI v49 sys_yield): 明示的な譲り。GUI 中は
 * tick の間引き無しで **必ず** WAIT_POLL へ park し、印 parked_from_yield を
 * 立てる (起こすとき注入リングを読まず EAX = 0)。成立すれば **戻らない**。
 * park できない文脈 (CUI / CPL=0 / syscall の外 / 入れ子の子) では `hlt` を
 * 1 回して 0 を返す。 */
i32 exec_sys_yield(void);

/* 止めてあるアプリを起こさずに畳む。0 / OS32_ERR_INVAL / OS32_ERR_STALE。
 * 票 T9 D8: **id とその子孫** (起動要求表の child を末尾まで辿ったもの) を
 * **末尾から** 畳む。CTRL+STOP のように 1 本だけ止めたいときは、WM が
 * launch_child() で末尾を解決してその ID を渡す。 */
i32 exec_kill(i32 app_id);

/* 0 = 空き / 1 = 走っている / 2 = park 中 (OP_WAIT) / 3 = kbd 待ち /
 * 4 = ポーリングの譲り / OS32_ERR_INVAL。3 は K7 の、4 は T8 D8 の追加で、
 * 既存の 0〜3 の意味は動かない。 */
i32 exec_app_state(i32 app_id);

/* CTRL+STOP (IRQ1 が走っているアプリに立てた要求) を降ろす (KAPI v45、A1)。
 * 0 = 降ろした / 要求が無かった、OS32_ERR_INVAL = owner 1 以外。 */
i32 exec_abort_clear(void);
int ring3_wait_pending(void);

/* KAPI sys_getcwd の実体 (票 T9 §12 R1)。CPL=3 の呼び手には
 * トランポリンページ内の写しを、CPL=0 の呼び手には fs/vfs.c の static cwd を
 * 返す。カーネル帯には USER ビットが無いので、写さずに返すと CPL=3 側が
 * 読んだ瞬間に #PF → fault kill になる。sdk/kapi.json の target をこれに
 * 差し替えてあるだけで、スロット・引数・戻り型は不変 ([ABI2])。 */
const char *vfs_cwd_user(void);
/* vfs_devname の CPL=3 向けの写し (KAPI vfs_devname の実体、TASK_HDD_INSTALL 段 2) */
const char *vfs_devname_user(const char *prefix);
/* path_get_drive / path_get_cwd の CPL=3 向けの写し (KAPI の実体、同上) */
const char *path_get_drive_user(void);
const char *path_get_cwd_user(void);

/* 上の写し場の番地とページ属性をブート時に踏む (票 T9 §12 R1)。
 * kselftest_run() は exec_init() より前に走るので、この項だけ
 * kselftest_run_post_exec() から呼ぶ。0 = 全部通った。 */
u32 exec_tramp_user_selftest(void);
/* データ欄の固定配置と予約スロット (票 TASK_KAPI_DATA_FIELDS)。
 * ビット 0 = 固定オフセット、1 = 本物の表の予約 (NOSYS)、2 = トランポリンの
 * 予約スタブ、3 = ヘッダ v3 の照合。0 = 全部通った。exec_init の後に呼ぶ。 */
u32 exec_kapi_layout_selftest(void);

/* KAPI 踏み台ページ (RO+USER、全 PD 共有) の番地。exec_init 前も静的 BSS の番地を返す。
 * ページ表と memmap.h の照合 (paging_memmap_selftest) が期待値に使う —
 * ここは .bss の中なのでビルドごとに動き、定数では書けない。 */
u32 exec_tramp_page_addr(void);

/* 直前の**入れ子 0 段の**起動 (常駐シェル / gshell の exec_run) が KAPI データ
 * 欄の配置違い (OS32X ヘッダ v3 の kapi_data_off、票 TASK_KAPI_DATA_FIELDS) で
 * 断られたなら 1。0 段の起動に入るたびに 0 へ戻し、シェルから起動したアプリの
 * 拒否では立たない。kernel.c が常駐シェルを断ったときの案内
 * (「/sys を作り直して配備せよ」) に使う。 */
int exec_layout_rejected(void);

/* ======================================================================== */
/*  ユーザポインタの検証 (票 S0-K §1a、KAPI v50)                             */
/*                                                                          */
/*  int 0x80 の早期検証 (kapi_argptr) は NULL を通し、先頭番地だけを見る。   */
/*  長さ付き引数は早期検証から外し、生成 wrap / target が NULL と全域を検査。*/
/*  NUL 探しが要る文字列を写す wrap も、写す前に範囲を確かめること。         */
/* ======================================================================== */

/* p が CPL=3 アプリへ USER で貸してある帯にあるか (先頭 1 番地だけ)。
 * NULL は 1 (wrap 側が意味を決める)。ディスパッチャの早期検証と同じ規則。 */
int ring3_ptr_ok(u32 p);

/* Read permission through the fixed caller's managed page walk (PDE/PTE
 * PRESENT|USER and registered backing). No CR3 switch. CPL0/WM direct calls
 * retain their explicit trusted convention. NULL/overflow/NP refuse. */
int ring3_user_range_ok(u32 p, u32 len);

/* 出力引数として渡された CPL=3 の番地に**書いてよいか**。
 * OS32 は CR0.WP = 0 なので、読み取り専用の USER ページ (共有ライブラリの
 * .text) への CPL=0 からの書き込みは #PF にならない — 帯の検証だけでは
 * 止められない (Codex 往復 10)。戻り 0 のときは書かずに kill する。
 * B1 の管理frame walkで PDE/PTE 両方の RW を確認する。 */
int ring3_user_range_writable(u32 p, u32 len);

/* Two outputs preflighted in one IRQ interval, without switching CR3.
 * A zero length needs no output. Checking is not a reservation across yield. */
int ring3_user_ranges_writable(u32 va, u32 la, u32 vb, u32 lb);

/* 保存済み USER caller で検査する。WM 中も trusted にはしない。
 * 登録済み redirect ポインタには使わない: 登録者 PD の redir_access を使う。
 * この関数は「いま渡されたポインタ」の既存出力ガード用。 */
int ring3_user_ranges_writable_always(u32 va, u32 la, u32 vb, u32 lb);

/* いまの呼び出しが CPL=3 のアプリ由来か (= ring3_guard_active(ring3_in_syscall,
 * ring3_wm_depth))。ポインタを**あとで使うために控える** KAPI が、控える時点で
 * 由来を記録するのに使う (redir_access_capture の RedirAccess)。 */
int ring3_call_from_user(void);

/* Internal raw-disk KAPI authorization; no public ABI field. */
int exec_disk_write_allowed(void);

/* CPL=3 アプリを fault として畳む (fault_kill_count++ → master CR3 復帰 →
 * AS 破棄 → longjmp)。**戻らない。** 実体は exec/exec.c。
 * 帯違反と同じ扱いにしたい KAPI ラッパだけが呼ぶ。 */
void ring3_fault_kill(void);

/* 断った理由 (ring3_range_reject_last)。実機で KAPI が MISUSE を返したときに
 * どのサブ条件だったかを 1 回の起動で確定させるための観測点 — KAPI にはせず
 * カーネルシンボルとして `emu_read_mem` で読む (fault_kill_count と同じ形)。 */
#define RING3_RANGE_NULL       1   /* p == 0 */
#define RING3_RANGE_OVERFLOW   2   /* p + len が折り返す */
#define RING3_RANGE_NO_APP     3   /* ring3_in_syscall なのに g_cur_app が 0 */
#define RING3_RANGE_BAND       4   /* ring3_ptr_ok の許可帯の外 */
/* 5 / 6 は PTE 検査をしていた頃の理由。いまは使わない (番号は再利用しない —
 * 実機のログと突き合わせるとき意味が変わると困る)。 */
#define RING3_RANGE_NOPRESENT  5   /* (廃止) 帯の中だが非 present */
#define RING3_RANGE_NOUSER     6   /* (廃止) present だが USER 無し */
/* 7〜10 は書き側 (ring3_user_ranges_writable) の理由 (2026-09-26 に追加。それ
 * までは書き側は数えておらず、wrap_mouse_poll で kill されても count が 0 の
 * ままだった)。addr = 断ったポインタ、page = 見ていたページ (TRIVIAL は 0)。 */
#define RING3_RANGE_WR_TRIVIAL 7   /* NULL か p + len の折り返し (長さ > 0) */
#define RING3_RANGE_WR_TABLE   8   /* 保存caller無効 / 管理walk拒否 (d5: write拒否を集約、page=0) */
#define RING3_RANGE_WR_PDE     9   /* PDE に present / RW / USER が無い (か PS) */
#define RING3_RANGE_WR_PTE    10   /* PTE に present / RW / USER が無い */
extern volatile u32 ring3_range_reject_count;
extern volatile u32 ring3_range_reject_last;
extern volatile u32 ring3_range_reject_addr;
extern volatile u32 ring3_range_reject_page;
extern volatile u32 ring3_range_reject_heap_top;

/* 現在のネスト深度 (0=外部プログラム未実行) */
extern volatile int exec_nest_level;

/* カーネルが WM (gshell) のコードへ入っている深さ (exec/exec.c の定義の注記)。
 * 入口 (gui_call のハンドラ / ポンプ / owner_exit) で enter、出口で leave。
 * 3 つの門は ring3_guard_active(ring3_in_syscall, ring3_wm_depth) で判定する。 */
extern volatile int ring3_wm_depth;
void ring3_wm_enter(void);
void ring3_wm_leave(void);
/* 深さ 0 での leave (enter と対になっていない出口) の回数。0 のままが正常。
 * 増えたら「WM のハンドラの中から exec_start / exec_resume を呼んだ」(契約 T1
 * 違反 — 入れ子の syscall がディスパッチャの入口で深さを 0 に戻す) か、
 * enter/leave の対が崩れた。KAPI にはせずカーネルシンボルとして読む
 * (ring3_park_reject_count と同じ流儀)。 */
extern volatile u32 ring3_wm_depth_underflow;
/* WM の文脈 (深さ 1 以上) でアプリをフォールトで畳んだ回数 (exec.c の注記)。
 * WM の中で落ちるとアプリの kill として畳まれるので、それを見分ける印。 */
extern volatile u32 ring3_wm_fault_count;
/* Entry identity mismatch, separate from ordinary pointer/fault kills. */
extern volatile u32 ring3_caller_reject_count;

#endif /* __EXEC_H */
