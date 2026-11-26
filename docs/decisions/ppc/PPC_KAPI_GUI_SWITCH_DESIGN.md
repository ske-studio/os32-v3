> 出所: ~/os32-tmp/run/kfill-5d/DESIGN.final.md (設計ラリー 2 回、起草 Fable・レビュー sol、2026-11-20 確定)
> 状態: **現行 (2026-11-20)** — 段 5d の確定設計。末尾の「確定」の節は本文に優先する

# 段 5d 詳細設計 r2 — 次のシェルの要求 (sys_switch_shell) を PowerPC で公開する

基点: origin/main b36d921d2 (レビューは cc1260bd2)。前の段: 5b・5c・7a (origin/wip/kfill-designs c0f3b3dce)、DISPLAY_HOOKS_DESIGN。
書き方: 関数・マクロ・型は実コードで照合した。新しく作る物は **新設** と書く。読んでいない物は §7。

## r1→r2 の変更 (REVIEW.r1 の指摘ごと、と決定の反映)

- **r1 の誤り (自分で見つけた、全体に効く)**: r1 は「PowerPC の sh は owner 1」としたが違う。`pm_exec_boot` が `exec_nest_level = 1` にし (sys/platform/powermac/exec_boot.c の pm_exec_boot「The kernel boot context is the parent (ID 1)」)、sh.bin は `is_shell` でない**アプリ** (ID 2〜5、cpl3=1、sys/kern/exec/exec_launch.inc の exec_launch)。→ 今の門 (sys/kern/gui.c の sys_switch_shell) は PowerPC の sh も断る。ID 1 は終わらないので `gui_owner_exit` の owner 1 の枝 (sys/kern/gui.c の gui_owner_exit) にも届かない。§1 の #3・#10、§2 を書き直した。
- **ユーザー決定 U1 (2026-11-20)**: gui_register・ime_set_render は PowerPC で無いまま、**sys_switch_shell の 1 本で 5d を完了**。gui_call は決定の文面に無いので不在のまま残す (§6 の PM 確認 1)。
- **ユーザー決定 U3**: 切替先が失敗したら既定の sh に戻す。入力が元どおり使えることを kboot-sh・link-sh の両方で確かめる (A7・A8)。**PM 決定 U2**: GUI の実行の主体は延期の一覧へ。
- P1-1 (文字列の契約): 入口を足さず、**既に在る共通の写し** (sys/kapi/kapi_strings.c の inputs が `KAPI_SLOT_SYS_SWITCH_SHELL` を `{1, 1, VFS_MAX_PATH}` で登録、copy_caller_cstr で写してからラッパーへ) を契約として §2-3 と §3 の E 行に書いた。空・読み取りの失敗・NUL なし・範囲外・成功時だけ更新の順を 1 行ずつ。反論 1。
- **PM 決定 (P1-1 への返答)**: パスの文字列は **x86 と同じ扱い**、写しは足さない。確かめた結果: PowerPC の入口は x86 の CPL=3 のアプリと**同じ共通の道**を通る (sys/arch/ppc/kernel_trap.c の ppc_trap_dispatch → `exec_syscall_dispatch` → sys/kern/exec/exec_syscall.inc の exec_kapi_special)。ただしその道は、文字列の slot では早期の範囲検証 (sys/kern/exec/exec_syscall.inc の exec_syscall_dispatch) でなく、その手前の既存の写し (sys/kapi/kapi_strings.c の inputs・kapi_strings_from_source) が受ける — x86 のアプリも今そうなっている。gui.c が直に読むのは CPL=0 で直呼びする x86 のシェルだけ。揃える作業は無く、5d は何も足さない (§2-3)。
- P1-2 (common.mk の条件が FEATURE_FEP の確定より前): **空の提供者 ime_control_absent.c を取り下げた**。終了のフックを選ばないので gui_owner_exit は PowerPC のリンクで捨てられ (build/targets/ppc-powermac.mk の KB_FLAGS `-ffunction-sections`、リンク時の `--gc-sections`)、ime_set_render の参照が残らない。後でフックを選ぶ段の形は §2-5 に 1 行 (build/select.mk の FEATURE_FEP の後で選ぶ)。
- P1-3 (test_geforce.py (旧版 4b690286e6b4) の main の目印): **sys/kern/gui.c の gui_owner_exit を変えない** (表示の境目のマクロを取り下げた)。目印はそのまま。後で変える段の形は §2-5 に 1 行。
- P2 (切替先が次の要求を書いて異常終了): §3 の L 行を「前の結果」と「要求の有無」の独立の 2 軸に直し、A4 に引継ぎと 1 度だけの消費を足した。
- P2 (shutdown と端末の復帰の観測): gfx_shutdown の呼び出しを足しも変えもしないので対象が無くなった。sh の fault の表示は今のまま機種フック (sys/platform/powermac/console_text.c の display_provider_owner_exit)。
- P2 (M5 を戻す変異): フックを選ばないので M5 を取り下げた。反論 2。
- P2 (像ごとの公開): §2-4 に kboot-sh・link-sh・kboot-shell ごとの公開・消費・答えを固定した。
- P2 (担当と観測の手順): ppc_sh_boot.py・test_ppc_link_sh.py を P3 に、プローブの登録・RAM root・CHECK の登録を P4 に割り当てた (§5-1・§5-3)。

## 1. 実物の確認

### 1-1. 計画の前提と実コードの食い違い (10 件。r1 の 9 件から #3 を直し #10 を足した)

| # | 前提 | 実物 | 効き方 |
|---|---|---|---|
| 1 | 「exec の終了のフックを実装」(計画の 5d) | フックは在る (sys/kern/exec/exec_gui.h の exec_gui_pump・exec_gui_owner_exit、不在の実体 sys/kern/exec/absent/exec_gui.c の exec_gui_pump・exec_gui_owner_exit、選ぶ値 build/sources/hooks.mk の EXEC_HOOKS) | 作る物は無い |
| 2 | gui.c は PowerPC に無い | C_COMMON_PC98_PENDING (build/sources/common.mk の C_COMMON_PC98_PENDING)。PowerPC のコンパイルは通っている (build/targets/ppc-powermac.mk の KB_PENDING_OBJ) | 置き場は今のまま、組の移動だけ |
| 3 | sh が gui_register・sys_switch_shell を呼べる | sh は ID 2〜5 のアプリ (上の「r1 の誤り」)。gui_register の門 (sys/kern/gui.c の gui_register) は owner でも由来 (`ring3_call_from_user`、sys/kern/exec/exec.c の ring3_call_from_user) でも断る。sys_switch_shell の門 (sys/kern/gui.c の sys_switch_shell) は owner で断る | sys_switch_shell は「セッションのシェルか」を機種が答える境目が要る (§2-2) |
| 4 | 「shell→GUI→shell の寿命」 | `gui_take_next_shell` (sys/kern/gui.c の gui_take_next_shell、宣言 sys/kern/gui.h の gui_take_next_shell) の呼び手は sys/platform/pc98/kernel.c の kernel_shell_loop だけ。PowerMac は固定の文字列 (sys/platform/powermac/shell_boot.c の pm_shell_boot) | PowerMac の取り出し手が要る (P2・P3) |
| 5 | PowerPC の sh は GUI を知らない | sh.bin は userland/shell/*.c 全部 (build/ppc-user.mk の PPC_SH_SRC)。`os32gui` (userland/shell/cmd_sys.c の cmd_os32gui) が sys_switch_shell → 0 なら sys_exit(0) | 公開で `os32gui` の動きが変わる (U3 で決定済み) |
| 6 | DISPLAY_HOOKS §3 「gui.c が入れば console_return まで届く」 | owner 1 の枝は PowerPC では届かない (ID 1 は起動の文脈)。sh の終了の表示は機種フック (sys/kern/exec/exec_finish.inc の exec_notify_owned → sys/platform/powermac/console_text.c の display_provider_owner_exit) が既に受けている | 追加の作業は本当に無い。sys/kern/gui.c の gui_owner_exit は触らない |
| 7 | gui.c は EXEC の物だけで結べる | `ime_set_render` (sys/include/ime_control.h の ime_set_render) は FEP=0 の像に無い (定義は sys/kern/ime/fep_module.c の ime_set_render・sys/kern/ime/ime.c の ime_set_render) | 参照するのは gui_owner_exit・gui_ime_set_render だけ。どちらも PowerPC では呼び手が無く捨てられる (§7-2 で確かめる) |
| 8 | 受入「登録者に要求が一度届き…」(計画の 5d 受入) | PowerPC に登録者は無い | U1 の決定で対象外。受入は切替の往復と入力の戻り |
| 9 | 7a「ime_set_render は GUI の後」 | 入口 sys/kern/gui.c の gui_ime_set_render は #3 と同じ門 | 出さない (U1) |
| 10 | (r1 が見落とした) 終了のフックを選べば効く | PowerPC では登録者が居らず (g_gui_handler は 0 のまま)、owner 1 も終わらない → `exec_gui_owner_exit`・`exec_gui_pump` は**届いても何もしない** | **HOOK_gui は 5d では切り替えない** (不在のまま)。GUI の実行の主体 (U2、延期) の段で選ぶ |

### 1-2. 統合の判断 3 点 (POLICY_DEV §1)

| 点 | 判断 |
|---|---|
| 既に在る物で足りるか | 記録と取り出し (sys/kern/gui.c の g_next_shell・sys_switch_shell・gui_take_next_shell)、文字列の写し (kapi_strings.c)、起動 (exec_run、sys/kern/exec/exec.h の exec_run) は在る。足すのは「セッションのシェルか」の境目と PowerMac の取り出し手だけ |
| 同じ処理が増えないか | 要求のバッファ・検査・取り出しは gui.c の 1 か所のまま。PowerMac に文字列の検査を持ち込まない (計画の共通化方針) |
| 計画・確定設計と合うか | 計画の GUI の提供条件 の「GUI 実行主体を先行」は延期 (U2)。5d は実行主体が無くても正しい 1 本だけを出す (U1 の決定) |

## 2. 公開の形と口

### 2-1. 組の扱い (U1 の決定)

| slot | 5d の後の PowerPC |
|---|---|
| sys_switch_shell (sys/kern/gui.c の sys_switch_shell) | **公開**。sdk/kapi_target_ppc.py の PROVIDER_GROUPS の `'gui'` の組から抜く |
| gui_call・gui_register・ime_set_render | 不在のまま (NOSYS・無動作)。`'gui'` の組に 2 本残る |

### 2-2. 境目: 「呼び手はセッションのシェルか」

```c
/* platform/pc98/platform_session_shell.h (新設) */
#define SESSION_SHELL_CALLER() (res_owner_get() == GUI_SHELL_OWNER)        /* 今の門と同じ式 */
/* platform/powermac/platform_session_shell.h (新設) */
int pm_session_shell_caller(void);
#define SESSION_SHELL_CALLER() pm_session_shell_caller()
/* include/session_shell.h (新設。sys/include/display_provider.h の platform_display_fallback.h の取り込み と同じ形で像ごとのヘッダを選ぶ) */
#include <platform_session_shell.h>
/* sys/kern/gui.c: include を 1 行足し、sys_switch_shell の owner 判定 の 1 行だけ置き換える */
    if (!SESSION_SHELL_CALLER()) {          /* 旧: if (res_owner_get() != GUI_SHELL_OWNER) { */
```
x86 は展開後の式が同じ (関数差 0 を期待)。gui.c のほかの行は変えない。`res_owner_get` は sys/kern/gui.c の res_owner_get の extern、`GUI_SHELL_OWNER` は sys/kern/gui.h の GUI_SHELL_OWNER。

```c
/* platform/powermac/session_shell.c (新設、C_PLATFORM_EXEC = build/platform/powermac.mk の C_PLATFORM_EXEC に登録) */
static int consumer;                                   /* 取り出し手が居る像だけ 1 */
static char next[OS32_MAX_PATH];
int pm_session_shell_caller(void)
{   /* 起動の文脈 (ID 1) が直に起こしたアプリだけ。sh の子は親が sh なので偽 */
    return consumer && ring3_call_from_user() &&                       /* sys/kern/exec/exec.h の ring3_call_from_user */
           appslot_return_target(appslot_cur()) == APP_ID_SHELL;       /* sys/kern/exec/appslot.h の appslot_return_target・appslot_cur、sys/include/app_ids.h の APP_ID_SHELL */
}
const char *pm_shell_select(const char *def, int *switched)            /* 戻り値は必ず非 NULL */
{
    consumer = 1;
    *switched = gui_take_next_shell(next, OS32_MAX_PATH);              /* 0/1。取り出して消す */
    return *switched ? next : def;
}
```

### 2-3. 文字列の契約 (P1-1。新しい入口は作らない)

PowerPC の KAPI は全部 `exec_syscall_dispatch` を通る (sys/arch/ppc/kernel_trap.c の ppc_trap_dispatch)。文字列の slot は `exec_kapi_special` (sys/kern/exec/exec_syscall.inc の exec_kapi_special) → `kapi_strings_from_source` (sys/kapi/kapi_strings.c の kapi_strings_from_source) が先に受ける: 呼び手が USER で ID 2〜5 であることを確かめ (kapi_strings_from_source)、`path` を `kmalloc(VFS_MAX_PATH)` の控えに `copy_caller_cstr` (sys/include/redir_access.h の copy_caller_cstr、実体 sys/kern/exec/redir_access.c の copy_caller_cstr) で写し、**控えの番地**でラッパーを呼び、戻ったら控えを返す (文字列の解放)。VFS_MAX_PATH = OS32_MAX_PATH = 256 (sys/fs/vfs.h の VFS_MAX_PATH)。失敗は -1 → `ring3_fault_kill` (sys/kern/exec/exec_syscall.inc の exec_syscall_dispatch)。gui.c の本体は門 (sys_switch_shell の owner 判定) → 空の検査 (path の先頭) → 長さ (長さの上限) → **最後に** `kstrncpy` (バッファ更新) なので、要求が変わるのは全部通った時だけ。

### 2-4. 像ごとの公開・消費・答え (P2)

| 像 | EXEC / USERSHELL | 取り出し手 | sys_switch_shell の答え |
|---|---|---|---|
| kboot-sh・kboot-link-sh | 1 / 1 (sys/conf/images/kboot-sh.mk の FEATURE_EXEC・FEATURE_USERSHELL、sys/conf/images/kboot-link-sh.mk の FEATURE_EXEC・FEATURE_USERSHELL) | pm_shell_boot (sys/platform/powermac/ppc_kernel.c の platform_boot_run) が毎周 pm_shell_select | sh と切替先: §3 の E 行。その子: INVAL |
| kboot-shell | 1 / 0 (常駐の CLI、sys/platform/powermac/ppc_kernel.c の platform_boot_run) | 無い (consumer は 0 のまま) | 誰が呼んでも INVAL (切り替えるシェルが無い)。要求は残らない |
| kboot-core・kboot-link | 0 / 0 | — | exec が無く、表も無い |

### 2-5. 取り下げた物の、後の段での形 (GUI の実行の主体の段への申し送り)

- フックを選ぶ時の ime_set_render の空の提供者: 選ぶ条件は build/select.mk の FEATURE_FEP (`include sys/conf/features/fep.mk` の `endif`) の**後ろ**に置く (common.mk は build/select.mk の IMAGE_FEATURES で先に読まれ、FEATURE_FEP の既定値はその後に決まる)。
- sys/kern/gui.c の gui_owner_exit を機種のマクロに替える時: test_geforce.py (旧版 4b690286e6b4) の stage が抜く `function(gui,'gui_owner_exit')` の前に PC-98 のマクロの定義行を足し、gui_owner_exit の変異 の目印を新しい行へ移して赤を確かめる。

## 3. 失敗の表 (全部の出口)

E = sys_switch_shell (PowerPC、USER の呼び手)。上から順に判定される。

| # | 原因 | 判定の場所 | 戻り値 | 要求 (g_next_shell) | 呼び手の次の状態 | 再試行 |
|---|---|---|---|---|---|---|
| E1 | 引数の列が読めない | sys/kapi/kapi_strings.c の kapi_strings_from_source | — (kill) | 変わらない | FAULT で畳まれる | 不可 |
| E2 | path が NULL | 写しを飛ばす (nullable、NULL の枝) → 門 → sys/kern/gui.c の sys_switch_shell | 門が偽なら INVAL、真でも INVAL | 変わらない | 続く | 引数を直して可 |
| E3 | path が読めない番地・範囲外・途中で写像が切れる | copy_caller_cstr が 0 (コピー失敗) | — (kill) | 変わらない | FAULT で畳まれる (門より前。子でも畳まれる) | 不可 |
| E4 | 256 バイトの中に NUL が無い | 同上 (cap で打ち切り) | — (kill) | 変わらない | FAULT | 不可。sys/kern/gui.c の sys_switch_shell の NAMETOOLONG は USER からは届かない (CPL=0 の直呼びの x86 のシェルだけ) |
| E5 | 控えの kmalloc の失敗 | kapi_strings_from_source の一時領域割当 | — (kill) | 変わらない | FAULT (文字列の slot 全部の今の動き) | 不可 |
| E6 | セッションのシェルでない (sh の子、kboot-shell の全員) | sys/kern/gui.c の sys_switch_shell | INVAL | 変わらない | 続く | 不可 |
| E7 | 空文字列 | sys/kern/gui.c の sys_switch_shell | INVAL | 変わらない (前の要求も残る) | 続く | 可 |
| E8 | 成功 | sys/kern/gui.c の sys_switch_shell | 0 | 上書き (後勝ち、1 個)。控えは返る | 続く。終わるまで何も起きない | — |

sh が E1・E3〜E5 で畳まれた時はループの L の「前の結果 = FAULT」の行に入る。

L = PowerMac のループ (pm_shell_boot)。**次の周の動きは「要求の有無」だけで決まり、止めるかは「前に起こしたのが既定か切替先か」と結果で決まる** (2 軸を独立に)。

| # | 前に起こした物 | 前の結果 | 止めるか | 次の周 (要求あり) | 次の周 (要求なし) |
|---|---|---|---|---|---|
| L1 | 既定の sh | EXITED | 止めない | 切替先を 1 回起こす (要求は消える) | 既定の sh |
| L2 | 既定の sh | FAULT | 非 LINK: **止まる** (今のまま、要求は取り出されず残る)。LINK: 止めない | LINK: 切替先 | LINK: 既定の sh |
| L3 | 既定の sh | ABORTED・起動しない (4〜7) | 止まる (今のまま、両方の像) | — | — |
| L4 | 切替先 | EXITED | 止めない | 新しい切替先 (後勝ちの 1 個) | 既定の sh |
| L5 | 切替先 | FAULT・ABORTED | 止めない | **切替先が残した要求を引き継いで**起こす (1 度だけ) | 既定の sh |
| L6 | 切替先 | 起動しない (NOT_FOUND ほか。例: /bin/gshell.bin が無い) | 止めない (U3) | (起動していないので新しい要求は無い) | 既定の sh |
| L7 | 切替先のコマンド行が LAN の端末を起こさない | — | — | 引数は足さない (x86 も足さない) | 終われば既定 (LAN 付き) に戻る |

回り続けないこと: 要求は取り出すと消える (sys/kern/gui.c の gui_take_next_shell) ので、同じ要求は 2 度起きない。切替先が毎回自分を要求して終わる時だけ回る (x86 と同じ、呼び手の作り)。

```c
/* shell_boot.c: 既定のシェルの選択と実行 の 2 行を 1 個の DEFAULT_CMD にまとめ、ループの中を次の順に */
        cmd = pm_shell_select(DEFAULT_CMD, &switched);
        (void)exec_run(cmd);
        (void)exec_last_result(&kind, &code);     /* sys/kern/exec/exec.c の exec_last_result */
        report(kind, code);
        if (switched) continue;                   /* L4〜L6 */
        /* 既定の sh の 2 行の break は字面を変えない (目印: test_ppc_link_sh.py (旧版 043afe0103f4) の main、ppc_sh_boot.py (旧版 5a9698ae3a9a) の main) */
```

## 4. 部品の大きさと x86 の差の見込み

| 物 | 大きさ | 根拠 |
|---|---|---|
| gui.o (x86) | text 678 / bss 296、変わらない | 手元の build/out/obj/sys/kern/gui.o の実測 (木の版は §7-4) |
| gui.c の PowerPC の像に残る分 | sys_switch_shell・gui_take_next_shell と g_next_shell (bss 256)。0.3 KB 前後 | 見込み。P5 でリンクマップから実測 |
| session_shell.o | text 0.2 KB 未満、bss 260 | 見込み |

x86 の build_compare: M1・M2 (ヘッダだけ)、M3 (展開後が同じ式)、M4 (x86 は C_COMMON の集合が同じ、リンク順は build/sources/order.mk の C_ORDER_LEGACY) → **全段 関数差 0・kernel.bin 一致を期待**。差が出たら PM に戻す。**x86 の答えが変わる所: 無し。**

## 5. ファイルごとの変更と段

### 5-1. 段 (1 ファイル = 1 段)

| 段 | 担当 | ファイル | 中身 | 前提 |
|---|---|---|---|---|
| M1 | 本線 | platform/pc98/platform_session_shell.h (新設) | §2-2 のマクロ | — |
| P1 | PPC | platform/powermac/platform_session_shell.h (新設) | §2-2 の宣言とマクロ | — |
| M2 | 本線 | include/session_shell.h (新設) | 像ごとのヘッダを選ぶ 1 行 | M1・P1 |
| M3 | 本線 | sys/kern/gui.c | include 1 行と sys_switch_shell の owner 判定 の置き換え。gui.c を単体で組む host の試験の include の道も同じ段 | M2 |
| P2 | PPC | platform/powermac/session_shell.c (新設) + build/platform/powermac.mk の C_PLATFORM_EXEC | §2-2。host の試験 (門の真偽の表) も同じ段 | P1 |
| M4 | 本線 | build/sources/common.mk | sys/kern/gui.c を PENDING (C_COMMON_PC98_PENDING) から C_COMMON_EXEC (C_COMMON_EXEC) へ。hooks.mk は**触らない** | M3・P2 |
| P3 | PPC | sys/platform/powermac/shell_boot.c + ppc_sh_boot.py (旧版 5a9698ae3a9a)・test_ppc_link_sh.py の土台 | §3 のループ。土台に pm_shell_select の stub、A4 の場面 | P2・M4 |
| P4 | PPC | userland/ppc/swprobe.c (新設)・build/ppc-user.mk への登録・旧受入 ppc_switch.py (新設)・旧登録 ppc-switch-guest.py (新設、inputs・covers・重みの登録) | A5〜A9。5c-C0 と同じ仮の結線 (台本が公開表の写しから 1 本を抜いて組み、KB_ROOT_BINS (build/targets/ppc-powermac.mk の KB_ROOT_BINS) に swprobe を足す) | P3 |
| P5 | PPC | sdk/kapi_target_ppc.py | `'gui'` から sys_switch_shell を抜く | P4 が緑 |

### 5-2. 前提の段の表

| 出す物 | 要る物 | 無い時 |
|---|---|---|
| P5 (公開) | P3 (取り出し手)・M3 (境目)・§2-3 の写しが PowerPC の像で動くこと (A6) | 出さない |
| M4 | --gc-sections で gui_owner_exit・gui_ime_set_render が捨てられること (§7-2) | リンクが未解決で止まる → 止めて報告 (空の提供者は §2-5 の形) |
| gui_call・gui_register・ime_set_render・HOOK_gui | GUI の実行の主体 (延期) | 5d では出さない |
| 5b・5c・7a | 5d はどれのコードにも依らない (順だけ) | — |

### 5-3. 触る既存の検査と固定 (n-1b。着手の時にもう一度 grep)

| 変える名前 | 当たる検査・固定 | 直し方 |
|---|---|---|
| sys/kern/gui.c の sys_switch_shell の owner 判定 と include (M3) | gui.c を実物で組む: test_ring3_guard.py (旧版 8e0bfe6189d3) の KERNEL_SRCS (目印 登録と切替の変異 は sys_switch_shell の owner 判定 を使わない。登録・切替の変異 の**置き換え後**の字面が旧 sys_switch_shell の owner 判定 と同じ形だったので、一意性の数え方を確かめる)、test_trim_kernel.py (旧版 e2ed82192ce3) の TARGET_SRCS・build_fixture・main、test_x11_shared.py (旧版 81caa9f12417) の SOURCES、test_d2_services.py (旧版 a368a577f699) の contracts、test_trim_flow・test_trim_back_rs・test_kstop・test_gf5.py (旧版 e11bfb5c2301) の function。関数だけ抜く test_geforce.py (旧版 4b690286e6b4) の stage・main は当たらない | session_shell.h が読めるよう `-Iplatform/pc98` か stub を土台ごとに足す |
| common.mk (M4) | build_profile_ppc.json (旧版 80da850b3508) (gui.o が入る。`sys/kern/exec/absent/exec_gui.c` は 3 像に**残る**ことを固定)・build_profile_x86.json (不変)、test_build_selection.py、test_sep_identity.py、test_ppc_rshell_link.py、旧検査登録 の exec-image-host・build-selection-host・check-select-host | fixture の作り直し |
| shell_boot.c・session_shell.c (P2・P3) | ppc_sh_boot.py (exec_run の stub が "/bin/sh.bin" を照合、目印 既定パスの検査)、test_ppc_link_sh.py (旧版 043afe0103f4) の CHECK・main、tools/ppc/link_sh_run.py の BOOT・app_started、sc_run.py・usb_run.py・fb_run.py、arch_ctx.py (旧版 73f25956448a)、test_ppc_sd_run.py、旧検査登録 の ppc-sh-boot・ppc-sh-output・ppc-sc・ppc-sd・ppc-usb・ppc-fp-guest・ppc-cli-boot | 目印の行は残す。単体で組む土台に stub |
| sdk/kapi_target_ppc.py (P5) | test_kapi_binding.py (旧版 3baaeeee4857) の EXPECTED_ABSENT_SLOTS・insert_group・generate、test_kapi_table.py、旧検査登録 の ppc-kapi-binding・ppc-kapi-table・ppc-reset・ppc-stream-host、test_arch_compat.py・test_pcm_contract.py・test_remote_x9.py | 不在の並びから 1 本を抜く |

### 5-4. 受入 → 試験と変異

| # | 受入 | 試験 | 変異 (赤) |
|---|---|---|---|
| A1 | x86 は変わらない | build_compare 差 0、CI。既存の gui.c の host の試験が緑 | PC-98 のマクロを `1` に (owner 1 以外が通る: multiapp・ring3_guard の既存の照合か、足す 1 件) |
| A2 | 門の真偽 (consumer 0 / 由来が USER でない / 親が sh / 全部真) | host: session_shell.c を stub で組む (P2) | 親の照合を消す、consumer を見ない |
| A3 | 取り出しは 1 度だけ・後勝ち | host: 実物の gui.c で 2 回要求 → take が後の 1 個 → 次の take は 0 | sys/kern/gui.c の gui_take_next_shell を消す |
| A4 | ループの L1〜L6 (結果 × 要求の 2 軸)。切替先が要求を書いて FAULT → 引き継いで 1 度だけ起こす | host: ppc_sh_boot.py (P3) | `if (switched) continue;` を消す、pm_shell_select の取り出しを消す |
| A5 | sh の子からは INVAL、要求は残らない (E6) | QEMU: sh から `swprobe child` → その後 sh を exit → 既定の sh が起きる | A2 の変異像 (切替が起きる) |
| A6 | 文字列の契約 (E2・E3・E4・E7・E8): セッションのシェルとして NULL・空 → INVAL、有効 → 0。別の起動で読めない番地・NUL なし → プローブだけ畳まれ、カーネルと次の sh は生きる | QEMU: swprobe を /bin/gshell.bin の名で RAM root に置き `os32gui` で起こす | 公開表で提供者を外す (-10 に戻る) |
| A7 | 往復と入力の戻り (U3): sh → `os32gui` → swprobe が `/bin/hello.bin` を要求して exit → hello → 既定の sh → 1 行打って結果が返る | QEMU 7450・7455、**kboot-sh (USB の打鍵) と link-sh (LAN の端末のつなぎ直し) の両方** | 取り出しを消す |
| A8 | 切替先が無い時 (L6、U3): gshell.bin を置かずに `os32gui` → 「sh failed kind=4」→ 既定の sh に入力が通る | 同上、両方の像 | `if (switched) continue;` を消す (kboot-sh が止まる) |
| A9 | kboot-shell は誰が呼んでも INVAL | A2 の host で consumer 0 の行。guest は常駐 CLI から swprobe を起こせれば足す (§7-5) | — |
| A10 | 型別の欠如 | gui_call・gui_register・ime_set_render が不在のまま (test_kapi_binding) | 組を空にする (gui_call・gui_register に効く) |

## 6. 決定と未決

| # | 件 | 状態 |
|---|---|---|
| U1 | gui_register・ime_set_render は PowerPC で無いまま、sys_switch_shell の 1 本で 5d を完了 | **ユーザー決定 (2026-11-20)** |
| U2 | PowerPC の GUI の実行の主体の設計 | **PM 決定**: 延期の一覧へ。HOOK_gui の切替・gui_call・gui_register・ime_set_render もそこへ |
| U3 | 切替先が失敗したら既定の sh に戻す。入力は両方の像で確かめる | **ユーザー決定 (2026-11-20)**。A7・A8 |

ユーザー判断が要る未決: **0 件**。PM に確かめる 2 件 (機種の中の決め、x86 は変わらない):
1. gui_call は U1 の文面に無いので不在のままにした。出すなら P5 に 1 本足すだけ (答えは INVAL・NOSYS、sys/kern/gui.c の gui_call)。
2. 「セッションのシェル = 起動の文脈 (ID 1) が直に起こしたアプリ」(§2-2) と、kboot-shell の答えを NOSYS でなく INVAL にしたこと (§2-4)。

## 7. 確かめていない物 ([V4])

1. `appslot_return_target` (sys/kern/exec/appslot.c の appslot_return_target) は親が死んだ子にも APP_ID_SHELL を返す。PowerPC で親より長く生きる子が作れるか (exec_start の門 sys/kern/exec/exec.c の exec_start は owner 1 だけなので sh からは作れない見込み)。作れるなら門を `AppSlot.parent` (sys/kern/exec/appslot.h の APP_RUNAWAY_TICKS) の読み口に替えて PM に戻す。
2. M4 の後、FEP=0 の PowerPC の 3 像で gui_owner_exit・gui_ime_set_render が捨てられてリンクが通ること (字面からの判断)。
3. check_layers が gui.c → include/session_shell.h → 像ごとのヘッダの形を通すこと (display_provider.h と同じ形という判断)。
4. gui.o の x86 の実測は手元の木。5c が main に入っているかは見ていない (5d は依らない)。
5. kboot-shell の guest でプローブを起こす手段、link-sh で sh が入れ替わった後の LAN の端末のつなぎ直しの既存の手順 (link_sh_run.py の fault の場面の続き)、RAM root に別名で置く台本の作り。
6. gui.c を単体で組む host の土台の数 (M3 の着手の時に数える)。

## 反論

1. P1-1「ラッパーは path をそのまま渡し、先頭番地だけ検査する。PowerPC の入口で copy_caller_bytes を」: 文字列の slot は早期の範囲検査 (sys/kern/exec/exec_syscall.inc の exec_syscall_dispatch) より前に kapi_strings_from_source が受け、sys/kapi/kapi_strings.c の inputs に SYS_SWITCH_SHELL が登録済みで、全文を copy_caller_cstr で控えに写す。新しい入口は同じ処理の 2 個目になるので作らず、出口の表 (E1〜E8) と受入 (A6) だけ採った。 PM 決定の「早期範囲検証を通ることを書く」も同じ事実で読み替えた: 通るのは早期検証でなく既存の写しで、これは x86 のアプリと同じ道なので「x86 と同じ扱い・足さない」は満たしている (PowerPC だけ強くしてはいない)。
2. P2「HOOK_gui を戻す変異で赤を取る」: 食い違い #10 のとおり PowerPC ではフックが届いても何もしないので、選ばない。代わりに fixture で `sys/kern/exec/absent/exec_gui.c` が残ることを固定する (§5-3)。

## PM の確かめ (2026-11-20)
- gui_call は不在のまま (ユーザー決定 U1 の「sys_switch_shell の 1 本で完了」に沿う)。
- 「セッションのシェル = ID 1 が直に起こしたアプリ」の定義と、取り出し手の無い kboot-shell の答えを INVAL にすることは、起草者の案どおり PM 決定。
- 文字列の slot は既存の共通の写し (sys/kapi/kapi_strings.c) を通り、PowerPC も x86 の CPL=3 のアプリと同じ道 — 足す物は無い (起草者の反論 1 を受ける)。

## 確定 (2026-11-20、sol 第 2 回、PM が反映)
以下は本文に優先する。
- **P1 (§3 L4–L6、§5-4 A4・A6、U3)**: 切替先の起動の失敗・FAULT・ABORTED では、その間に書かれた保留の要求を破棄して既定の sh に戻す。正常な終了 (EXITED、終了コードは 0 でも非 0 でも) では今の後勝ちの契約どおり、最後の要求があればそれへ、無ければ既定の sh へ。A4・A6 の期待をこれに揃える (FAULT の直前に書いた要求は採らない)。
- **P2 (§2-2、§7-1)**: 「ID 1 の直下」の判定は `appslot_return_target()` でなく、`appslot_get(appslot_cur())` が返す `AppSlot *` の `parent` (sys/kern/exec/appslot.c の appslot_get) で行う。
- **P2 (§2-3、§3 E1・E6、§5-4 A9)**: E1 は「呼び手の文脈の取得の失敗」に改める (固定引数は保存済みの GPR から読む、sys/arch/ppc/kapi_call.c の arch_kapi_read_block)。kboot-shell の INVAL は文字列の共通の写しを通った入力に限る (不正な文字列はその前に FAULT)。A9 のゲストの確かめは常駐の CLI 経由の swprobe で (sys/platform/powermac/cli_boot.c の pm_cli_boot)。
- **P2 (§5-4 A4・A10)**: 取り出しを消す変異は実体を通る A7 で RED を取る。A10 には ime_set_render 単独の公開の変異を足す (sdk/kapi_target_ppc.py の PROVIDER_GROUPS の ime の組)。

## 止める (2026-11-20 夕、ユーザー)
「(PowerPC の SHELL_AS_APP・SH_SYS_MINIMAL の sh は) 最初の PowerPC での動作確認に作った実装です。破棄して x86 に寄せてください」。この設計は PowerPC の仮の sh (ID 1 の直下の USER のアプリ) を前提にしていたので止める。本線の M1〜M4 (機種が答える SESSION_SHELL_CALLER()、wip/kfill-5d-m・m4) と PowerPC の P2 (platform/powermac/session_shell.c) は main に入れない。先に PowerPC のシェルを x86 のシェルの枠・起動のループに寄せ (票 TASK_PPC_ALIGN_X86 の B03・B09・B15)、その後 sys_switch_shell の公開を x86 と同じ門 (owner 1) で出し直す。
