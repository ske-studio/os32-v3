/* ======================================================================== */
/*  APPSLOT.H — アプリ ID の表と状態遷移 (K5b-K、票 TASK_K5_multiapp.md D0〜D4)*/
/*                                                                          */
/*  「同時に生きているアプリは 4 本まで、走るのは常に 1 本」を成り立たせる    */
/*  背骨。exec_ctx_stack の「ネスト段のスタック」を **ID で引く表** に置き    */
/*  換えたもの (I14) で、次の 3 つを 1 か所に集めてある:                     */
/*                                                                          */
/*    1. ID の池 (1 = シェル帯 / 2〜5 = アプリ)。空きは必ず小さい方から      */
/*       配るので、CUI の入れ子 exec_run では従来どおり **段 = ID** になる。 */
/*    2. 状態遷移 (FREE / RUNNING / PARKED) と、その遷移が許される文脈。      */
/*       park は `gui_call(OP_WAIT)` の中からだけ、resume は「OP_WAIT で     */
/*       park された印のあるフレーム」に対してだけ (D0 の C5/C6)。           */
/*    3. 受入 G7 のカウンタ (C1〜C3/C6)。KAPI にはしない — kernel.map の      */
/*       番地を emu_read_mem で読む (fault_kill_count と同じ形)。            */
/*                                                                          */
/*  ここにはハードウェア (CR3 / setjmp / pgalloc / ページテーブル) を         */
/*  1 つも置かない。実体は exec/exec.c が持ち、この表は「誰が居て、どの状態で、*/
/*  次に何をしてよいか」だけを決める。だからホストでそのまま試験できる       */
/*  (tools/tests/multiapp_impl_host.c が K5a の模型と同じ 84 検査を回す)。   */
/* ======================================================================== */

#ifndef __APPSLOT_H
#define __APPSLOT_H

#include "types.h"
#include "paging.h"        /* struct addrspace (値で持つ。関数は呼ばない) */
#include "ksetjmp.h"       /* KSETJMP_BUF_LEN */

/* ---- ID の池 (D3) ---------------------------------------------------- */
/* 1 = シェル帯 (CUI シェル / gshell)。kernel/gui.h の GUI_SHELL_OWNER と同値。
 * 一致は exec/appslot.c の STATIC_ASSERT が固定する。 */
#define APP_ID_SHELL     1
#define APP_MAX_APPS     4                       /* 同時に生きる非シェル ID */
#define APP_ID_MIN       2
#define APP_ID_MAX       (APP_ID_MIN + APP_MAX_APPS - 1)   /* 5 */
#define APP_SLOT_COUNT   (APP_ID_MAX + 1)        /* 添字 = ID。0 は使わない */

/* ---- 状態 ------------------------------------------------------------ */
#define APP_STATE_FREE     0
#define APP_STATE_RUNNING  1
#define APP_STATE_PARKED   2
/* GUI 中の kbd 待ち = **第 2 の park 点** (票 K7 §1 D1)。PARKED と同じく
 * 「フレームを保存して WM へ戻した」状態だが、起こす条件が違う:
 * PARKED は WM が渡す wait_ret で起き、WAIT_KEY は**注入リングの 1 バイト**で
 * 起きる (exec_resume が EAX へ入れる。§5 の指摘 B)。値の追加なので
 * exec_app_state の既存の 0/1/2 は 1 つも動かない (§5 の指摘 C)。 */
#define APP_STATE_WAIT_KEY 3
/* GUI 中の**ポーリング型**の協調 yield = **第 3 の park 点** (票 T8 §7 D8)。
 * kbd_trygetchar / kbd_trygetkey は「無ければ -1」で戻る約束なので、
 * WAIT_KEY のように「キーが来るまで起こさない」わけにいかない —
 * 譲るのは **1 周だけ**で、WM は次の周に必ず起こし、そのとき注入リングが
 * 空なら EAX に -1 (キーなし) を書く。だから WAIT_POLL は WM から見て
 * **常に ready** (ただし優先度は最下位)。
 * 値の追加なので exec_app_state の既存の 0/1/2/3 は 1 つも動かない。 */
#define APP_STATE_WAIT_POLL 4

/* 暴走 (KAPI を呼ばない計算ループ) の逃げ道 (票 T9 §12 S6)。GUI 中の IRQ1 は
 * 「走っている ID が最後に **カーネルへ入って** からこの tick 数以上経った」
 * ときだけ CTRL+STOP を立てる。PIT は 100Hz なので 200 = 2 秒。
 * 生きているアプリは描画も入力もキーも KAPI 経由なので、2 秒 1 度も
 * カーネルへ入らない = KAPI を呼ばない計算ループに入った、と読める。
 * **待っているアプリは対象外** — op_wait / kbd 待ちは KAPI の中に居る
 * (その syscall の入口で控えが更新されている)。 */
#define APP_RUNAWAY_TICKS  200

/* int80_stub が積むフレームの語数 ([0..7]=pushad, [8]=EIP [9]=CS
 * [10]=EFLAGS [11]=userESP [12]=userSS)。ring3_entry.asm と同期。 */
#define APP_FRAME_WORDS    13
/* pushad の EAX スロット = 戻り値 (resume の wait_ret を書く先) */
#define APP_FRAME_EAX      7

typedef struct {
    int  state;               /* APP_STATE_* */
    int  parent;              /* 起動した側の ID (GUI アプリは常に 1) */
    int  depth;               /* 従来の exec ネスト段 (シェル = 1) */
    int  gui;                 /* 塞がない起動 (exec_start) で立った */
    int  cpl3;                /* CPL=3 で走っている (AS を持つ) */
    int  in_op_wait;          /* いま gui_call(OP_WAIT) の中に居る (C4) */
    int  abort_req;           /* CTRL+STOP 要求 (この ID 宛) */
    int  parked_from_wait;    /* park したフレームの「OP_WAIT 由来」の印 (C5) */
    int  parked_from_kbd;     /* park したフレームの「kbd 待ち由来」の印 (K7 D1) */
    int  parked_from_poll;    /* park したフレームの「ポーリング由来」の印 (T8 D8) */
    /* 明示的な譲り sys_yield 由来の印 (票 T9 D5)。状態は WAIT_POLL のままで、
     * 違うのは resume のときに **注入リングを読まない** こと — 読むと、sh が
     * 譲っている間に届いた「子宛の 1 バイト」を吸って捨ててしまう
     * (票 §6 blocker 1)。欄の追加なので exec_app_state の 0〜4 は動かない。 */
    int  parked_from_yield;
    /* この ID が最後に **カーネルへ入った** PIT tick (票 T9 §12 S6)。
     * 更新する 3 点: 起動 (start) / resume / **int 0x80 の入口**
     * (ring3_syscall_dispatch。代入 1 つだけの hot path)。
     *
     * かつては start / resume だけだったが、GetMessage 型の GUI アプリ
     * (端末) は WM が op_wait の中で回っている間 resume を通らないので、
     * **2 秒イベントを待っただけで「暴走」に見えた** (§12 S6b)。KAPI を
     * 呼んでいる限り最近カーネルへ入っているので、これを起点にすると
     * 「KAPI を呼ばない計算ループ」だけが APP_RUNAWAY_TICKS に掛かる。
     *
     * GUI 中の CTRL+STOP の宛先は WM が決める (D8) ので、カーネルが IRQ1 で
     * 畳むのはその暴走 1 例外だけ。 */
    u32  last_kernel_tick;

    u32  jmpbuf[KSETJMP_BUF_LEN];   /* この ID の呼び出し元へ帰る点 */
    u32  frame[APP_FRAME_WORDS];    /* park した CPL=3 フレーム (D2 の (b)) */

    /* 旧 ExecContext の中身 (レイアウトは 1 バイトも動かさない — I12/I13) */
    u32  guard_a;             /* sbrk ガード (= exec_heap の直下) */
    u32  guard_b;             /* スタックガード */
    u32  sbrk_heap_limit;
    u32  exec_heap_base;
    u32  exec_heap_size;
    u32  exec_heap_used;
    u32  load_addr;
    u32  stack_top;

    /* CPL=3 アプリ固有 (I10: 切替のたびに差し替える「現在のアプリ」の値) */
    u32  band_top;
    u32  band_pdes;
    struct addrspace as;
    u32  pages;               /* この ID が握っている物理ページ数 (D5) */

    /* 起動した OS32X ヘッダの flags (票 T8 D1a)。exec_launch が写す。
     * 見るのは OS32X_FLAG_GFX (全画面 GFX を使う宣言) だけで、
     * OS32X_FLAG_FORCE_CPL0 の判定は起動前 (appslot_launch_is_app /
     * appslot_cpl0_admit) に済んでいる。構造体の末尾に足すので、
     * 旧 ExecContext 由来の欄の並びは 1 バイトも動かない (I12/I13)。 */
    u32  hdr_flags;
} AppSlot;

/* ---- 受入 G7 のカウンタ (D8 の C1/C2/C3/C6) --------------------------- */
/* KAPI にしない。fault_kill_count / ring3_abort_count と同じくカーネル
 * シンボルとして公開し、PM の V4 検証が emu_read_mem で読む。 */
extern volatile u32 ring3_switch_count;            /* C1 resume 成功回数 */
extern volatile u32 ring3_transition_count;        /* C2 start/終了の CR3 遷移 */
extern volatile u32 ring3_park_reject_count;       /* C3 OP_WAIT 外の park */
extern volatile u32 ring3_resume_bad_frame_count;  /* C6 印無しフレームの resume */
/* GUI 中の kbd 待ちで park した回数 (票 K7 の受入 I1: 止まらずに譲れたか)。 */
extern volatile u32 ring3_kbd_park_count;
/* GUI 中のポーリング型 yield で 1 周だけ譲った回数 (票 T8 §7 D8 の受入 F8:
 * 「FPS 段の秒数 × 100 以下」で増えるか = tick の間引きが効いているか)。 */
extern volatile u32 ring3_poll_yield_count;
/* GUI 中に sys_yield で明示的に譲った回数 (票 T9 D5 の観測点)。tick の
 * 間引きが無い park 点なので、ポーリングの譲りとは別に数える。 */
extern volatile u32 ring3_yield_count;
/* 回収の回数と直前の対象 (試験と診断用。G2/G5 の「1 本分だけ」を数える) */
extern volatile u32 appslot_reclaim_count;
extern volatile int appslot_last_reclaim_id;
/* GUI 中に「宣言 (OS32X_FLAG_GFX) の無い CPL=3 が gfx_init を呼んだ」ので
 * 断った回数 (票 T8 D1a の受入 F6)。KAPI にはしない — kernel.map の番地を
 * emu_read_mem で読む。 */
extern volatile u32 gfx_init_reject_count;

/* ---- 表の操作 -------------------------------------------------------- */

/* 表を空にし、シェル帯 (ID 1) を走っている状態にする。exec_init から 1 回。 */
void appslot_init(void);

/* 生きているスロット (FREE なら 0)。id が範囲外でも 0。 */
AppSlot *appslot_get(int id);
/* FREE も含めて添字で引く (起動途中の書き込み用)。範囲外なら 0。 */
AppSlot *appslot_at(int id);

/* いま走っている ID (1 = シェル帯 / WM top-level)。 */
int appslot_cur(void);
/* 生きている非シェル ID の本数。 */
int appslot_live(void);
/* 空き ID を小さい方から 1 つ (無ければ 0)。まだ確保はしない。 */
int appslot_alloc_id(void);

/* ---- 起動 (D4) -------------------------------------------------------- */
/* この起動が **アプリ帯 (CPL=3、per-app 物理、0x500000〜)** を使うか。
 * 1 = アプリ帯 / 0 = そうでない (シェル帯の常駐 CPL=0、または --cpl0 の子)。
 *
 * is_shell (= exec ネスト段 0) は shell.bin でも gshell.bin でも **必ず 0**。
 * 常駐シェルは 0x300000 の MEM_SHELL_* 帯に identity で載る CPL=0 プログラム
 * で、per-app 物理化・ID の池・枚数勘定のどれにも掛からない (K5a 設計 D7
 * 「変えないもの」)。2026-09-11 の差し戻しで、この境界をホストで押さえる
 * ようにした (tools/tests/multiapp_impl_host.c ケース 18)。
 * hdr_flags は OS32X ヘッダの flags (OS32X_FLAG_FORCE_CPL0 を見る)。 */
int appslot_launch_is_app(int is_shell, u32 hdr_flags);

/* --cpl0 の子 (アプリ帯を identity で丸ごと押さえる CPL=0 の子) を起動して
 * よいか。**状態は 1 つも変えない**。
 *
 * 決裁 2026-09-11 (申し送り A1): --cpl0 の子は exec_cpl0_claim() で
 * [MEM_EXEC_LOAD_ADDR, mem_end) を丸ごと pgalloc_mark_used し、終了時に
 * 丸ごと free する。K5b-K 以後は CPL=3 アプリの per-app 物理も同じ pgalloc
 * から取るので、生きているアプリ (走行中 / park 中) が 1 本でも居ると、
 * その物理を上書きし、終了時に他人のページを解放してしまう。
 * 枚数で刻む機構は増やさず、**生存アプリが 1 本でも居たら拒否**する
 * (特権が要る例外用途なので、GUI のアプリを閉じてから使えば足りる)。
 *
 * 決裁 2026-09-12 (票 T8 D1): --cpl0 のプログラムは VRAM を直接触るので、
 * **GUI からの起動 (gui=1 = exec_start) は生存アプリの有無に関わらず拒否**
 * する。GUI 中に画面を丸ごと持っていかれると WM が復帰できない (画面の所有者
 * は gfx_init を呼ぶ CPL=3 アプリしか取らない)。CUI の exec_run (gui=0) は
 * 従来どおり「生存アプリが居なければ通す」のまま。
 *
 * 戻り値: 0 = 起動してよい / OS32_ERR_INVAL = GUI からは不可 (T8 D1) /
 *         OS32_ERR_FULL = 生存アプリが居るので不可。
 * シェル (exec ネスト段 0) はそもそもアプリ帯を使わないので対象外 —
 * 呼び出し側が appslot_launch_is_app() と同じく is_shell を渡す。 */
int appslot_cpl0_admit(int is_shell, int gui);

/* CUI 専用の宣言 (OS32X_FLAG_CUI_ONLY、mkos32x --cui-only / app.conf 4 列目
 * `cui`) を持つプログラムを起動してよいか。**純関数 — 状態は 1 つも変えない**。
 *
 * 票 T8-2 (受入 F5 の不合格を受けて、2026-09-12)。T8 D1 の砦は
 * appslot_cpl0_admit だけだったが、`userland/cmds/v86.bin` の flags は 0x0 —
 * v86 は **CPL=3 のプログラム**で、V86 へは KAPI (v86_selftest / v86_disktest /
 * v86_boot / v86_boot2) を通してカーネル側から入る。FORCE_CPL0 では捕まらない
 * ので、宣言ビットを 1 つ増やして GUI からの起動そのものを断つ。
 *
 *   gui       : exec_launch の gui_arg (1 = exec_start / 0 = CUI の exec_run)
 *   hdr_flags : OS32X ヘッダの flags
 * 戻り値: 0 = 起動してよい / OS32_ERR_INVAL = GUI からは不可。
 * シェル (ネスト段 0) は宣言を持たないので is_shell は要らない。CUI 中は
 * 宣言があっても素通し (「CUI に降りてから実行する」が決裁の趣旨)。 */
int appslot_cui_only_admit(int gui, u32 hdr_flags);

/* 起動してよいかを判定する。**状態は 1 つも変えない**。
 *   gui=1 (exec_start): WM の top-level からだけ (契約 S2)
 *   gui=0 (exec_run):   走っているアプリからも通る (決裁 D9-8)
 * 戻り値: >0 = 使ってよい ID / OS32_ERR_INVAL (S2 違反) /
 *         OS32_ERR_FULL (池が尽きた) / EXEC_ERR_NOMEM (物理が足りない)。
 * pages は要求する物理ページ数、free_pages は今の空き (呼び出し側が pgalloc
 * に聞いて渡す)。free_pages == 0 は「勘定しない」の合図 (CPL=0 の子)。 */
int appslot_start_admit(int gui, u32 pages, u32 free_pages);

/* admit した ID を実際に走らせる (iret の直前)。ring3_transition_count++。 */
void appslot_start_commit(int id, int gui, u32 pages);

/* シェル帯 (ID 1) を走らせる。親は無く、段は必ず 1 (K4 のシェル起動ループが
 * shell.bin / gshell.bin を載せ替えるたびに呼ばれる)。 */
void appslot_shell_commit(void);

/* ---- gui_call の文脈 (C4) --------------------------------------------- */
/* gui_call がハンドラを呼ぶ間だけ現在の op を控える。is_wait=1 で OP_WAIT。 */
void appslot_gui_op_enter(int is_wait);
void appslot_gui_op_leave(void);

/* ---- park / resume (D0 / D2) ----------------------------------------- */
/* park してよいか。ダメなら ring3_park_reject_count++ して負を返す。
 * 条件: 走っているのがアプリ (2〜5) で、いま gui_call(OP_WAIT) の中。 */
int appslot_park_check(void);
/* park を成立させる (フレームは呼び出し側が slot->frame へ写してから呼ぶ)。
 * 印 parked_from_wait を立て、PARKED にして cur をシェル帯へ戻す。 */
void appslot_park_commit(void);

/* GUI 中の kbd 待ち (第 2 の park 点、票 K7 D1) で park してよいか。
 * park_check との違いは「OP_WAIT の中」を要求しないことだけ — 呼び出しの
 * 文脈が gui_call ではなく kbd_getchar / kbd_getkey の syscall だから。
 * 走っているのが塞がない起動 (gui=1) のアプリであることは同じく要る
 * (CUI の入れ子 exec_run の子を park すると WM へ戻れない、D4 と同じ理由)。
 * ダメなら ring3_park_reject_count++ して負を返す。
 *
 * kbd_gui_mode / CPL=3 フレームの有無は **ドライバとカーネル側の条件** (R1)
 * なので、ここでは見ない (exec/exec.c と drivers/kbd.c が見る)。 */
int appslot_park_kbd_check(void);
/* kbd 待ちの park を成立させる。印 parked_from_kbd を立て、WAIT_KEY にして
 * cur をシェル帯へ戻す。ring3_kbd_park_count++。 */
void appslot_park_kbd_commit(void);

/* ---- 第 3 の park 点: ポーリング型の協調 yield (票 T8 §7 D8) ----------- */
/* GUI 中に注入リングが空のまま kbd_trygetchar / kbd_trygetkey が回っている
 * とき、**1 周だけ** WM へ譲ってよいか。park_kbd_check との違いは 1 つ —
 * **PIT tick の間引き** (10ms に 1 回まで) を持つこと。busy-wait のループから
 * 呼ばれるので、間引きが無いと譲りだけで CPU を食い潰す。
 *
 *   now_tick : 呼び手 (drivers/kbd.c) が渡す PIT の tick (100Hz)。
 *              **引数で受ける**のは (a) この表がハードウェアを知らない規約と
 *              (b) ホスト試験 (tools/tests/multiapp_impl_host.c ケース 22) で
 *              tick を差し替えられるようにするため。
 *
 * 戻り値: 0 = 譲ってよい / OS32_ERR_AGAIN = 前回の試みから tick が進んで
 *         いない (**弾き数に載せない** — 正常な間引きであって違反ではない) /
 *         OS32_ERR_INVAL = 表の側で不可 (ring3_park_reject_count++)。
 * 順番は「間引き → 表」で、控えは **間引きの側が進める** (弾かれた試みも
 * 数える)。逆にすると CUI の入れ子の子が回すたびに
 * ring3_park_reject_count が跳ね上がる (ポーリングは秒間数万回来る)。
 * **この関数は控えを進めるので純関数ではない** (park_check / park_kbd_check が
 * 弾き数を進めるのと同じ扱い)。
 *
 * kbd_gui_mode / CPL=3 フレームの有無は R1 と同じく呼び手側 (drivers/kbd.c と
 * exec/exec.c) が見る。 */
int appslot_park_poll_check(u32 now_tick);
/* ポーリング型の park を成立させる。印 parked_from_poll を立て、WAIT_POLL に
 * して cur をシェル帯へ戻す。ring3_poll_yield_count++ (控えは check が進めた
 * ままにする)。 */
void appslot_park_poll_commit(void);
/* 間引きの控えを 0 に戻す (GUI セッションの切替 = appslot_init /
 * kbd_set_gui_mode から)。 */
void appslot_poll_yield_reset(void);

/* ---- 第 4 の park 点: 明示的な譲り sys_yield (票 T9 D5) --------------- */
/* park_poll_check との違いは 2 つ: **PIT tick の間引きを掛けない** (明示的な
 * 譲りは呼び手の意思で、描画ループの busy-wait とは違う) ことと、印が
 * parked_from_yield になること。状態は WAIT_POLL のまま (WM から見れば
 * 「常に ready、優先度は最下位」で、起こし方の規則を増やさない)。
 * ダメなら ring3_park_reject_count++ して負を返す (呼び手は hlt 1 回へ)。 */
int appslot_park_yield_check(void);
void appslot_park_yield_commit(void);

/* resume のとき EAX に何を入れるか。印から導くので、対応表は 1 か所
 * (exec_resume が switch するだけ)。id が起こせない状態なら負。 */
#define APP_RESUME_SRC_WAIT   0   /* WM が渡す wait_ret */
#define APP_RESUME_SRC_KBD    1   /* 注入リングの 1 バイト。空なら起こさない */
#define APP_RESUME_SRC_POLL   2   /* 注入リングの 1 バイト。空なら -1 で起こす */
#define APP_RESUME_SRC_YIELD  3   /* 注入リングを**読まず** 0 (票 T9 D5) */
int appslot_resume_source(int id);

/* resume してよいか。WM top-level からだけ、印のあるフレームだけ。
 * PARKED は parked_from_wait、WAIT_KEY は parked_from_kbd、WAIT_POLL は
 * parked_from_poll **または** parked_from_yield (票 T9 D5) を要求する。印が無ければ ring3_resume_bad_frame_count++
 * して OS32_ERR_STALE (拒否は 3 つの park 点すべてに効く)。 */
int appslot_resume_check(int id);
/* resume を成立させる (CR3 を載せる直前)。印 (3 つとも) を消し
 * ring3_switch_count++。 */
void appslot_resume_commit(int id);

/* ---- 終了・kill (D4) -------------------------------------------------- */
/* この ID のスロットを空にする。戻り値は握っていた物理ページ数。
 * 資源 (FD / pipe / shm / db / GUI 窓) の回収は呼び出し側が ID で行う。 */
u32 appslot_reclaim(int id);
/* 畳んだ後に戻る先の ID (親が生きていれば親、居なければシェル帯)。 */
int appslot_return_target(int id);
/* cur を id へ移す (回収の後に親へ戻すとき)。ring3_transition_count++。 */
void appslot_switch_to(int id);

/* kill してよいか (top-level から、止めてあるアプリだけ)。 */
int appslot_kill_check(int id);

/* CTRL+STOP: 走っているアプリにだけ要求を立てる (D4)。1=立った。 */
int appslot_abort_request(void);

/* IRQ1 由来の CTRL+STOP を「いま走っているアプリ」に立ててよいか (**純関数**
 * — 状態を 1 つも変えない)。票 T9 §12 S6。
 *   gui_mode : con_sink_is_enabled() (1 = GUI 中 / 0 = CUI 中)
 *   now_tick : tick_count
 * CUI 中は常に 1 (K2 の逃げ道はそのまま)。GUI 中は 3 つに分かれる:
 *
 *   - top-level (シェル帯が走っている) → **0**。W の abort_at_top_level が
 *     raw リングから拾い、D8 の宛先を解決して exec_kill する。
 *   - アプリが gui_call(OP_WAIT) の中 (`in_op_wait`) → **1** (K5c)。割り込ま
 *     れた文脈は CPL=0 (WM のコード) なのでスタブの即 kill は起きず、要求は
 *     必ず WM のハンドラが先に見る — 本人宛なら break して syscall 出口で
 *     畳み、別宛なら exec_abort_clear で降ろして exec_kill(宛先)。ここを
 *     塞ぐと、連鎖の末尾が自分自身のとき誰も畳まなくなる (受入 S6 の 3 回目)。
 *   - アプリのコードが CPL=3 で実際に走っている最中 → **0**、ただし暴走
 *     (最後に**カーネルへ入ってから** APP_RUNAWAY_TICKS 以上) なら 1。
 *     立てると IRQ1 スタブの即 kill が D8 の宛先より先に畳んでしまうため。 */
int appslot_abort_admit(int gui_mode, u32 now_tick);

/* この ID がカーネルへ入った時刻を控える (暴走判定の起点)。exec.c が
 * appslot_start_commit / appslot_resume_commit の直後に tick_count を渡す。
 * int 0x80 の入口は hot path なので、そちらは exec.c が
 * g_cur_app->last_kernel_tick へ直に代入する (関数呼び出しを増やさない)。
 * tick は引数で受ける — この表はハードウェアを読まない (park_poll_check と
 * 同じ流儀)。 */
void appslot_mark_scheduled(int id, u32 now_tick);

/* CTRL+STOP の要求を降ろす (KAPI v45 exec_abort_clear の実体、決裁 A1)。
 * 呼べるのは owner 1 (シェル帯 = WM) だけ — それ以外は OS32_ERR_INVAL。
 * IRQ1 は「いま走っているアプリ」に無条件で立てるが、契約 T6 の宛先は
 * **フォーカス窓のアプリ**。別アプリが走っていたら WM がここで降ろし、
 * フォーカス窓の ID を exec_kill で畳む。対象は要求を負っている高々 1 本
 * (立てられるのは走っている 1 本だけなので) で、他の ID の状態は動かさない。
 * 戻り値: 0 = 降ろした / 要求が無かった、OS32_ERR_INVAL = owner 1 でない。 */
int appslot_abort_clear(void);

/* KAPI exec_app_state の実体:
 *   0=空き / 1=走っている / 2=park 中 (OP_WAIT) / 3=kbd 待ち /
 *   4=ポーリング譲り中 / 負=不正。
 * 3 は K7 の、4 は T8 D8 の追加。既存の 0〜3 の意味は 1 つも動かない。 */
int appslot_state(int id);

/* ---- 画面の所有者 (票 T8 D1 / D1a) ------------------------------------ */
/* 全画面 GFX を握っている ID。1 = シェル帯 (WM) / 2〜5 = アプリ。
 * gfx_init / gfx_init_200 の KAPI ラッパ (gfx/gfx_core.c) が取り、
 * その ID の回収 (exec_reclaim_owned) で 1 に戻る。CUI 中 (con_sink 無効)
 * は誰も取らないので常に 1。GFX 側に置かないのは、判定材料 (走っている ID /
 * CPL / ヘッダ flags) が全部この表にあり、ホストで試験できるため。 */
#define GFX_OWNER_WM     APP_ID_SHELL

/* gfx_init / gfx_init_200 が呼ばれたときの判定 (**純関数** — 状態を 1 つも
 * 変えない)。ホスト試験はここを直接叩く。
 *   gui_mode  : con_sink_is_enabled() (1 = GUI 中 / 0 = CUI 中)
 *   caller    : res_owner_get() — 呼び手の ID
 *   cpl3      : 呼び手が CPL=3 で走っているか
 *   hdr_flags : 呼び手の OS32X ヘッダ flags
 * 戻り値: >0 = その ID を所有者にして gfx_init を通す
 *          0 = 所有者は触らずに通す (CUI 中 / WM 自身 / CPL=0 の子)
 *         OS32_ERR_INVAL = 宣言が無いので拒否 (D1a。gfx_init を呼ばない) */
int appslot_gfx_claim_check(int gui_mode, int caller, int cpl3, u32 hdr_flags);

/* 上を「いま走っている ID」に対して適用し、結果を反映する。
 * 通れば 0 (所有者を取った場合も 0)、拒否なら OS32_ERR_INVAL を返して
 * gfx_init_reject_count++ する。gui_mode は呼び出し側が con_sink に聞く
 * (exec/ は -Iinclude を持つが、判定材料をこの表に閉じるため引数で受ける)。
 *
 * 票 T8-2: 拒否は **そのアプリを畳む** — appslot_abort_request() で abort_req
 * を立て、syscall 出口の ring3_abort_check() に畳ませる。拒否して続行させると
 * 描画 KAPI と VRAM 直書きで GUI を壊すことが実測で分かったため (受入 F6)。 */
int appslot_gfx_claim(int gui_mode);

/* 画面の所有者 (KAPI v48 gfx_screen_owner の実体)。誰でも呼べる。 */
int appslot_gfx_owner(void);

/* 回収 (exec_reclaim_owned から)。所有者がこの ID なら WM へ戻す。 */
void appslot_gfx_owner_exit(int id);

/* ---- 自己診断 (kernel/kselftest.c) ------------------------------------- */
/* 「印の無いフレームは resume できない」(票 K7 受入 I5 / K5b の C6) の負例を
 * ブート時に踏む。空きスロットを一時的に借りて、PARKED / WAIT_KEY /
 * WAIT_POLL の 3 つで
 * 印なし → OS32_ERR_STALE、印あり → 0 を確かめ、借りたスロットと
 * カウンタ・cur・owner を元に戻す。ビット 0..n が落ちた項目 (0 = 全部通った)。
 * 呼ぶのは exec_init() の前後どちらでもよい (触った状態は必ず戻す)。 */
u32 appslot_resume_mark_selftest(void);

/* 「画面の所有者は gfx_init で移り、回収で WM へ戻る」(票 T8 D1) と
 * 「GUI 中の宣言なしは拒否」(D1a) をブート時に踏む。借りたスロット・
 * 所有者・カウンタは必ず元へ戻す。ビット 0..n が落ちた項目 (0 = 全通過)。 */
u32 appslot_gfx_owner_selftest(void);

/* 「GUI 中の CTRL+STOP はカーネルが宛先を決めない、ただし暴走は畳む」
 * (票 T9 §12 S6) をブート時に踏む。借りたスロット・cur・owner は必ず元へ
 * 戻す。ビット 0..n が落ちた項目 (0 = 全通過)。 */
u32 appslot_abort_admit_selftest(void);

#endif /* __APPSLOT_H */
