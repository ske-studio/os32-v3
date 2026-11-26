> 出所: ~/os32-tmp/run/disp-hooks/DESIGN.final.md (Fable 起草、Opus 5.5 レビュー 2 回、2026-11-19 確定)。
> 状態: **確定 (2026-11-19)**。

# 表示の初期化・終了・復帰の口 — PPC_KAPI_FILL_DESIGN への追記 (r2)

## r1→r2 の変更 (REVIEW.r1 の ID ごと)

- A1: 終了時の復帰は配り手 gfx_emergency を使わず、案 (b) の機種フック `display_provider_owner_exit(id)` を appslot_gfx_owner_exit の直前に置く (§2-3、段 H6・H7)。実物で成り立つ (呼び手 3 か所は exec の中)。
- A2: display.c は `pm_console_park()` を呼ぶ。実体は console.c (段 P5 を console.c に、display.c は P7)。
- A3: 「park は ready=0 なら何もしない」「実際に止めた時だけ text_state を 0 にする」を規則にし、起動と復帰の順を書いた (§2-2)。
- A4: H4 の完了条件に test_gfx_policy_triggers.py と boot_splash_native_host.c の土台の stub を足した (§5)。
- A5: H1・H2・P2 の完了条件に fixture (build_profile_ppc.json と x86 側)・order.mk・powermac.mk・check-build-selection-host を足した (§5)。
- A6: HOOK_gfx の変異は test_build_selection で捕らえ、「復帰 → 所有の返却」の順は新しい host の試験で照合する (§6)。
- A7: 4k の前提「kcg.mk の kcg_module.c を PC-98 だけに残す」を 1 行にした (§2-1、PM が 4k に書き戻す)。
- A8: §3 に「shutdown の後に描画した時」の行を足した (gfx_started の実物つき)。
- A9: kapi_sys_exit・shell_print の未確認を解き、P6 の前提から外した。
- A10: 試験の数を 8 に直した。
- A11: H4 は区分 I、CI だけで受け NP21/W は次の節目に相乗り。
- A12: 古い target の close の戻りの扱いを 1 行足した (§2-2)。
- 未決: 2 件とも PM 決定で閉じた (§8)。

対象: PowerPC の PM の依頼 #25 (gfx_init / gfx_init_200 / gfx_shutdown)、#7 (終了のフック)、保留 2 件 (mode_set・flip・present、check_op)。
親: docs/decisions/ppc/PPC_KAPI_FILL_DESIGN.md §3 (段 4a・4k・4b・5a〜5d)。前提は依頼文のとおり (議論しない)。
読んだ範囲: 下の パスと関数 だけ (grep で絞った行)。全文は読んでいない。未確認の所は「未確認」と書く。

## 1. 実物の確認

### 1-1. 依頼の各件と実物

| 件 | 依頼の言い分 | 実物 (パスと関数) | 判定 |
|---|---|---|---|
| #25 | gfx_init・gfx_init_200 の本体が KCG を条件にする | 条件は本体でなく **KAPI の門** `gfx_kapi_claim` (sys/hal/gfx/gfx_core.c 内の判定が `kcg_module_ready()`)。本体 gfx_init_200 とカーネル内部の gfx_init は条件なし | 食い違い 1 |
| #25 | PowerMac をファイルのフォント (KCG1、font_source.c 系) につなぐ | KCG1 の読み手は drivers/video/kcg/kcg_file.c (kcg_file_load、 `kcg_file_api`)。sys/kern/console/font_source.c は font lease (font_lease_*) で字形の読み手ではない。PowerMac は起動で既に準備済み (sys/platform/powermac/ppc_kernel.c kcg_file_load、 gfx_init、 pm_console_attach) | 食い違い 2 |
| #25 | (暗黙) PPC の像に条件の関数が要る | `kcg_module_ready` の定義は sys/dev/video/kcg/kcg_module.c だけ (PC-98 の部品の読み込み)。PPC の像が今組めるのは門が未参照で --gc-sections に落ちるから (build/targets/ppc-powermac.mk、--no-undefined 付き)。公開表に載せた時点で未定義になる | 食い違い 3 |
| #25 続き | OF の shutdown が bind を外す | sys/platform/powermac/display_provider.c `shutdown()` (sync の公開取り下げ + of_fb_unbind) を後端の shutdown に登録。再 bind は  `prepare()` が saved から行う。復帰の口 `display_provider_restore_fallback` は PPC で常に 0。加えて OF の shutdown で adapter の世代を進め、`pm_console_text_ready` (sys/platform/powermac/display_dual.c) が偽になる — CUI が消える原因は unbind と古い target の 2 つ | 食い違い 4 (原因が 2 つ) |
| #7 | 終了のフックは面の回収と表示の復帰が空 | sys/kern/exec/absent/exec_gfx.c (2 関数とも 0 を返す)。ただし本物の `gfx2_owner_release` (sys/hal/gfx/v2/owner_release.c) は共通の一覧に既に入っている (build/sources/common.mk の gfx/v2/*.c)。空なのは build/sources/hooks.mk が gfx のフックを FEATURE_PC98_PENDING に連動させているから。`gfx_return_emergency` の sys/hal/gfx/gfx_emergency.c は common.mk (PC98_PENDING) | 食い違い 5 (共通の側に新しいコードは要らない) |
| #7 | PC-98 の今の終了の処理 | sys/kern/exec/exec_finish.inc `exec_gfx_owner_release` (失敗で addrspace を poison)、 `exec_gfx_return_emergency` (arm しているのは Trident だけ: sys/dev/video/trident/trident_module.c)、 `appslot_gfx_owner_exit` (sys/kern/exec/appslot.c、所有を WM へ)。sys/kern/gui.c は **GUI シェルの異常終了だけ** gfx_shutdown。CUI のアプリが gfx_shutdown せずに落ちた時に表示を止める処理は exec/ に無い (ring3_guard_host.c (旧版 e64d934e092e)「app exit keeps display」) | 食い違い 6 (PC-98 に「表示の復帰」は無い。テキスト面が別なので要らなかった) |
| 保留 | docs/tasks/ppc/ の FINDINGS の表示の項 | docs/tasks/ppc/ に FINDINGS の名のファイルは無い。docs/tasks/ppc・docs/decisions/ppc で `hw_mode` は 0 件 (在るのは docs/design/include/gfx_mode.md と docs/decisions/gui/HAL_V2_DESIGN.md)。近い記述は docs/decisions/ppc/PPC_FBCON_DESIGN.md (mode_set は同じ形の shadow の attach だけ、flip は同期転送だけ) | 食い違い 7 (保留の出所を確かめられない) |
| 4k | select.mk の FEATURE_KCG の条件を解く | 条件は 2 つ: build/select.mk (x86-pc98 以外は error) と FEATURE_PC98_DEVICES (PC98_DEVICES 必須)。親の表 は後者だけを書く。sys/conf/features/kcg.mk は kcg.c と kcg_module.c を一緒に選ぶので、解くだけだと PPC に部品の読み込みが入る | 食い違い 8 |

門が呼ぶ `kapi_sys_exit` (sys/kern/exec/exec_finish.inc) と `shell_print` (sys/kern/console/console.c) はどちらも EXEC の一覧で、PPC の像に在る (レビュー A9。r1 の「未確認」は取り下げ)。

### 1-2. 統合の判断 3 点 (POLICY_DEV §1)

| 点 | 判断 |
|---|---|
| 既存の機能・計画の有無 | 全部ある。門・gfx_shutdown・面の回収・復帰の配り手 (gfx_emergency.c) は共通の実装が在り、PPC は選んでいないだけ。計画は親の 4k・4b・5b〜5d |
| HAL で 1 つの実装にするか | する。共通の本体は 1 つ。機種が持つのは (a) 字形の供給元が読めるかの答え、(b) CUI へ戻す後端、の 2 つだけ。PC-98 の (b) は空 |
| 計画の向きと合うか | 合う。HAL 中心期 (装置は HAL の口の実装)、x86 と重複なし。x86 の関数の差は 0 を狙う |

## 2. 口の形

### 2-1. フォントの条件 (新しい口は足さない)

門の条件は sys/include/kcg_contract.h の `kcg_module_ready()` をそのまま契約の口として使い、意味を「字形の供給元が読める」に広げる (コメントだけ直す)。gfx_core.c は変えない。

```c
/* sys/include/kcg_contract.h — 1 = 字形の供給元 (PC-98: kcg.mod、PowerMac: KCG1) を読める。 */
int kcg_module_ready(void);
```

| | 提供者 | 中身 | 状態の持ち主 |
|---|---|---|---|
| PC-98 | sys/dev/video/kcg/kcg_module.c (今のまま) | `module_exports(kcg_handle) != 0` | kcg_module.c の handle |
| PowerMac | 段 4b の供給 TU (kcg_rom_read_ank・kanji を KCG1 から出す TU) に 1 関数足す | `return kcg_file_ready();` (kcg_file.c に load 済みの旗を返す 1 行を足す) | kcg_file.c |

機種の側の「準備」は起動の列に既に在る (PC-98: sys/platform/pc98/kernel.c の部品の読み込み、PowerMac: ppc_kernel.c)。門から準備を呼ぶ口は足さない (門は判定だけ)。
名前の付け替え (kcg_source_ready 等) はしない: 試験 8 ファイル (旧ホスト試験) が今の名前を使い、x86 に差が出る。

4k・4b との関係: **4k-i → 4k-ii → 4b → この追記の段 P6 (公開)** の順。4k の前提 (PM が 4k の票に書き戻す): 作業木 kfill-4k は select.mk の 2 条件を外したが kcg.mk は触っていないので、**sys/conf/features/kcg.mk の kcg_module.c を PC-98 だけに残す**。残さないと 4b で PPC に kcg_module_ready を足した時に重複定義になる (食い違い 8)。4b が PPC の `kcg_rom_read_*` と `kcg_module_ready` を同じ TU で出す。この追記は 4k・4b のファイルに触らない (4b の票に「kcg_module_ready も出す」の 1 行を足すだけ)。

### 2-2. 表示の復帰の機種フック (gfx_shutdown の末尾)

```c
/* sys/platform/pc98/platform_display_fallback.h — テキスト面は別のハードなので何もしない */
static inline void display_provider_console_return(void) { }

/* sys/platform/powermac/platform_display_fallback.h */
void display_provider_console_return(void);   /* 実体は sys/platform/powermac/console_text.c */

/* sys/hal/gfx/gfx_core.c gfx_shutdown() の最後 (gfx_reinit_surfaces(1) の後) に 1 行 */
    display_provider_console_return();
```

- 固定名の機種ヘッダは既に在る (platform/{pc98,powermac}/platform_display_fallback.h、display_provider.h が -Iplatform/$(PLATFORM) で読む)。新しいヘッダは作らない。
- PC-98 は空の inline なので x86 のコードは増えない。
- CUI を離れる側の口は既に在る: `display_text_hide()` (sys/kern/console/display_restore.h、共通の gfx_core.c が呼ぶ)。PowerMac は今は空 (sys/platform/powermac/display.c)。ここを「端末を止める (セルは保つ)」にする。

PowerMac の後端 (sys/platform/powermac/console_text.c、新規。ppc_kernel.c の列を関数に出して起動と復帰で共用):

```c
/* sys/platform/powermac/console.c — terminal はこのファイルの static なので口はここに置く */
int  pm_console_park(void);     /* fbcon_park(&terminal)。実際に止めたら 1、つないでいなければ 0 */
int  pm_console_retarget(struct gfx_console_target *t);   /* fbcon_retarget(&terminal, t) */
/* sys/platform/powermac/display.c */
void display_text_hide(void) { if (pm_console_park()) pm_console_text_state(1, 1, 0, 0); }
/* sys/platform/powermac/console_text.c */
int  pm_console_text_start(void);          /* 起動: font → unicode → gfx_init → open → attach → 色 → clear */
void display_provider_console_return(void) /* 復帰。通常文脈だけ */
{
    if (!pm_text_was_up || pm_console_text_ready()) return;   /* 起動前・既に戻っている → 何もしない */
    gfx_init();                               /* prepare() が of_fb を再 bind、世代が決まる */
    /* 古い target を close → 0 で埋める → gfx_console_open → pm_console_retarget (セルを保って全面
     * 描き直し) → pm_console_text_state(1,1,1,新しい世代)。失敗は起動と同じ「FB off」。 */
}
```

規則と順 (レビュー A3・A12):

- `fbcon_park` は ready=0 (つないでいない、cells が無い) なら何もせず 0 を返す。止めている時の 2 度目も何もしない。text_state を 0 にするのは実際に止めた時だけ。
- 起動の列: gfx_init (ppc_kernel.c) → 共通の gfx_core.c が display_text_hide → まだつないでいないので park は 0、text_state は触らない → open → attach → text_state(1,1,1,世代) で初めて 0→1。pm_text_was_up はここで立てる。
- アプリの gfx_init: display_text_hide → park が 1 → text_state(1,1,0,0)。
- 復帰の列: gfx_shutdown → console_return → 中の gfx_init がもう一度 display_text_hide を通るが、既に止まっているので何もしない → retarget → text_state(1,1,1,新しい世代)。
- 古い target の close: shutdown は head を外す。`gfx_console_close` (sys/kern/console/gfx_console.c) は面が引けなければ何も外さずに戻る。**戻りは無視してよい** (握っている物が無い)。面が引けたのに失敗した時だけ rc をログに出す。どちらでも target は 0 で埋めてから open する。

sys/kern/console/fbcon.c (PPC の像だけが選ぶ共通、build/platform/powermac.mk) に 2 口を足す:

```c
void fbcon_park(struct fbcon *c);      /* 描かずにセルだけ更新する状態へ。cells は放さない */
int  fbcon_retarget(struct fbcon *c, struct gfx_console_target *t); /* 同じ桁・行なら cells を保って全面 redraw。違えば -1 */
```

状態の持ち主: 端末のセルは fbcon、文字が出せるかは display_dual.c の text_ready・text_gen、bind の元は display_provider.c の saved。gfx_core.c は機種の状態を持たない。
復帰の後、PowerMac は gfx_started=1 (カーネルの端末が FB を持つ) に戻る — 起動直後と同じ状態。画面の所有者 (appslot の g_gfx_owner) には触らない。

### 2-3. 終了の処理への結線 (#7)

新しいコードは無く、選び方を 2 行変える。

```make
# build/sources/common.mk: sys/hal/gfx/gfx_emergency.c を C_COMMON_PC98_PENDING から C_COMMON_EXEC へ (中身は変えない)
# build/sources/hooks.mk: 既存の font_lease・surface と同じ形で 1 行
HOOK_gfx = $(FEATURE_EXEC)
```

これで PPC も sys/kern/exec/exec_gfx.h の inline (gfx2_owner_release・gfx_return_emergency) を通り、sys/kern/exec/absent/exec_gfx.c は PPC の一覧から外れる (ファイルは gfx を持たない像のために残す)。
終了時の表示の復帰は、配り手 gfx_emergency を**使わない** (PM 決定、レビュー A1: sys/hal/gfx/gfx_emergency.h の契約は「IF=0 でも呼ばれる、確保・ログ・描画をしない」で、owner 0 は常に一致するので、端末のつなぎ直しを載せられない)。通常文脈だけの機種フックを 1 本足す:

```c
/* sys/platform/pc98/platform_display_fallback.h */
static inline void display_provider_owner_exit(int id) { (void)id; }
/* sys/platform/powermac/platform_display_fallback.h — 実体は console_text.c */
void display_provider_owner_exit(int id);
/* 契約 (両方のヘッダに書く): exec の終了の通知の列からだけ呼ぶ。割り込み・例外の中から呼ばない。
 * 確保・描画・ログをしてよい。id は終了するアプリ。所有の返却 (appslot_gfx_owner_exit) の前。 */

/* sys/kern/exec/exec_gfx.h — OS32_HOOK_GFX=1 の側 (display_provider.h を読む) */
static inline void exec_gfx_owner_exit(int id) { display_provider_owner_exit(id); }
/* =0 の側は宣言だけ、sys/kern/exec/absent/exec_gfx.c に空の実体 */

/* sys/kern/exec/exec_finish.inc — exec_gfx_return_emergency の後、appslot_gfx_owner_exit(id) の直前に 1 行 */
    exec_gfx_owner_exit(id);

/* sys/platform/powermac/console_text.c */
void display_provider_owner_exit(int id)
{
    if (id == appslot_gfx_owner() && !pm_console_text_ready()) gfx_shutdown();   /* 末尾で console_return */
}
```

- (b) が実物で成り立つ根拠: exec_notify_owned の呼び手は sys/kern/exec/exec_finish.inc と sys/kern/exec/exec_schedule.inc の 3 か所で、どれも exec の列の中。x86 の例外の中の呼び手 (レビューの isr_handlers.c) は gfx_return_emergency を直に呼ぶ別の道で、このフックは通らない。3 か所が通常文脈であることは H7 の実装者が確かめて報告する。
- 所有者の比較は機種の側でする (共通の側で appslot_gfx_owner() を呼ぶと x86 に呼び出しが 1 つ増える)。PC-98 は空の inline なので x86 の差は 0。
- gfx_emergency.c は EXEC へ移すだけ (exec_gfx.h の inline が参照する)。PowerMac は登録しないので gfx_return_emergency は 0 を返す。
- 順: 面の回収 (exec_finish.inc) → 配り手 → **機種フック** → 所有の返却。

## 3. 振る舞いの表

| 場面 | PC-98 (変わらない) | PowerMac (この追記の後) |
|---|---|---|
| gfx_init (KAPI) | 門: kcg.mod が無ければ「KCG unavailable」で exit(1)。GUI 中に宣言なしなら畳む。通れば gfx_init | 同じ門。KCG1 が読めなければ exit(1)。通れば gfx_init → display_text_hide が端末を止め (セルは保つ)、アプリが FB を使う |
| gfx_init_200 (KAPI) | 同じ門 → 200 行 | 同じ門。200 行の中身は段 5b (親設計の「200行指定の実現可能範囲を照会」) が決めるまで**公開しない** |
| gfx_shutdown | 後端の leave・shutdown、GeForce なら 98 へ戻す、面の作り直し。テキスト面はそのまま見える | 後端の shutdown (unbind) → 面の作り直し → console_return が再 bind・端末を新しい世代へつなぎ直して全面描き直し。直前の CUI の文字が戻る |
| アプリの正常終了 (shutdown 済み) | 面の回収 → 配り手は何もしない → 所有を返す | 同じ。text_ready が真なので復帰は何もしない |
| アプリの正常終了 (shutdown 忘れ) | 面の回収 → 所有を返す。グラフィック面は出たまま、文字は上に出る | 面の回収 → 機種フック owner_exit → gfx_shutdown → CUI に戻る |
| アプリの異常終了 (fault・kill・CTRL+STOP) | 上と同じ。Trident なら登録済みの復帰 | 上と同じ (機種フック)。例外の中では何もせず、exec の終了の列に来てから戻す |
| shutdown の後、終了の前に描画した時 | gfx_shutdown が gfx_started=0 にする (gfx_core.c)。gfx_get_framebuffer の系は active=0 を返す (sys/hal/gfx/gfx_core_api.inc、gfx_core_framebuffer.inc)。表示は止まっているので画面には出ない | 復帰の中の gfx_init で gfx_started=1 に戻り (gfx_core_init.inc)、所有者は終了までアプリのまま。v1 の present が公開されていれば端末の上に描ける。**今は出ない**: gfx_present ほか v1 の描画は PPC で未公開 (段 5a・5b)。段 5b の受入に「shutdown の後の present は端末の画素を変えない」を足し、5b が口 (所有者と世代の照合) を決める。v1 の描画 KAPI の個々の門は読んでいない (未確認) |
| 面の回収の失敗 | addrspace を poison (exec_finish.inc) | 同じ (共通) |
| CUI への復帰に失敗 | — | 起動の失敗と同じ扱い: 「FB off step=… rc=…」をログへ、端末はシリアル・LAN だけで続く。止まらない |
| GUI シェルの異常終了 | sys/kern/gui.c が gfx_shutdown | gui.c は段 5d。入れば同じ行が console_return まで届く (追加の作業なし) |

## 4. 像の大きさ

| 像・部品 | 増え方 | 上限に触れるか |
|---|---|---|
| x86 のカーネル (vmkernel) | 0 B を狙う (空の inline、一覧の移動だけ。順は build/sources/order.mk が持つ) | 起動 FD (1,474,560 B ちょうど)・MINIMAL に影響なし |
| kdebug.bin・kcg.mod ほかの部品 | 0 B (触らない) | 触れない (KDEBUG 262,144 の 42%、docs/08_build.md) |
| PowerPC の shell 像 (実測) | text +26.8 KB、bss +73.7 KB (gfx2_sw_rowbuf 32 KB ほか)、常駐 +0x18000 | 常駐 0x18f000 / 0x275000、chain_end 0x2fa000 / 0x3e0000 |

## 5. ファイルごとの変更と段

| 段 | 列 | ファイル (1 段 1 ファイル) | 中身 | 先に要る段 | 完了条件に足す追従 |
|---|---|---|---|---|---|
| H1 | 本線 | build/sources/common.mk | sys/hal/gfx/gfx_emergency.c を PC98_PENDING → EXEC へ | 一覧の持ち主から受け取る (親設計 §3) | fixture (build_profile_ppc.json に足す、x86 側の fixture の置き場の移動)、check-build-selection-host |
| H2 | 本線 | build/sources/hooks.mk | `HOOK_gfx = $(FEATURE_EXEC)` | H1・H6 | build_profile_ppc.json の sys/kern/exec/absent/exec_gfx.c 3 か所を外す、check-build-selection-host |
| H3 | 本線 | sys/platform/pc98/platform_display_fallback.h | 空の inline 2 本 (console_return・owner_exit) と契約のコメント | — | — |
| H4 | 本線 | sys/hal/gfx/gfx_core.c | gfx_shutdown の末尾に 1 行 | H3・P3 | test_gfx_policy_triggers.py (旧版 729757ba7bc5)  (gfx_shutdown を切り出す土台) と boot_splash_native_host.c (gfx_core.c を include) に console_return の stub |
| H5 | 本線 | sys/include/kcg_contract.h | kcg_module_ready のコメントの意味を広げる (コードなし) | — | — |
| H6 | 本線 | sys/kern/exec/exec_gfx.h (対の sys/kern/exec/absent/exec_gfx.c も同じ段) | exec_gfx_owner_exit の inline と空の実体 | H3・P3 | — |
| H7 | 本線 | sys/kern/exec/exec_finish.inc | appslot_gfx_owner_exit の直前に 1 行。呼び手 3 か所の文脈を報告 | H6 | exec_gfx_* を自前の stub にしている host の試験 (gfx2_exec_host.c、trident_emergency_host.c、app_bb_overlap_host.c、appmem_map_host.c、allocator_bench_host.c、r1_recovery_host.c) に stub を足す |
| P1 | PPC | sys/kern/console/fbcon.c | fbcon_park・fbcon_retarget (+ fbcon.h の宣言は同じ段) | — | fbcon_host |
| P2 | PPC | sys/platform/powermac/console_text.c (新規) | pm_console_text_start・console_return・owner_exit。ppc_kernel.c の列を移す | P1・P5 | build/platform/powermac.mk と build/sources/order.mk への登録、build_profile_ppc.json、check-build-selection-host |
| P3 | PPC | sys/platform/powermac/platform_display_fallback.h | 2 本の宣言と契約のコメント | — | — |
| P4 | PPC | sys/platform/powermac/ppc_kernel.c | 端末の起動処理を pm_console_text_start の呼び出しに替える | P2 | — |
| P5 | PPC | sys/platform/powermac/console.c | pm_console_park・pm_console_retarget (+ pm_console.h の宣言) | P1 | — |
| P6 | PPC | PPC の公開表 (sdk/kapi_target_ppc.py の系。test_kapi_binding.py (旧版 3baaeeee4857) の「無いはず」の一覧も同じ段) | gfx_init・gfx_shutdown と gfx2 の面・lock・submit・fence 10 本を公開 | 4k-i・4k-ii・4b、H1〜H7、P1〜P7 | 前後の像の大きさを報告 |
| P7 | PPC | sys/platform/powermac/display.c | display_text_hide を「止めた時だけ text_state を 0」に | P5 | — |

- 並列に出せる組: {H1}、{H3}、{H5}、{P1}、{P3} は互いに別ファイル。H6 は H3・P3 の後、H7 は H6 の後、H2 は H1・H6 の後 (HOOK_gfx=1 で PPC が exec_gfx.h の inline を通るので、機種の実体 P2 も先)。P5 は P1 の後、P2 は P5 の後、P4・P7 はその後、H4 は H3・P3・P2 の後。
- 4k との順: H1〜H7・P1〜P5・P7 は 4k・4b と独立で先に出せる (門は公開まで GC で落ちる)。4k・4b を待つのは P6 だけ。select.mk・kcg.mk は 4k だけが触り、この追記は触らない。
- NP21/W: H4・H7 は x86 の関数の差 0 を期待するので区分 I (ROLES §4-2「NP21/W を省く段」)。**CI だけで受け**、NP21/W の確認 (gfx_init → gfx_shutdown のアプリ、GUI の出入り) は本線の次の節目に相乗りする。差が出たら区分が変わるので PM に戻す。
- x86 の build_compare で期待する差: H1〜H7 は**差 0** (空の inline、一覧の移動、コメント)。H4・H7 で関数に差が出たら inline になっていないので直す。P の段は x86 に差 0。

## 6. 受入 → 試験と変異

| 受入の項目 | 試験 | 変異 (赤になること) |
|---|---|---|
| 門は字形の供給元が読めない時に本体を呼ばず exit(1) | host: gfx_kernel_fb_host の門の組を PPC の供給 TU でも組む (4b の試験に足す) | kcg_module_ready を常に 1 に |
| PPC の終了で面が回収される | host: test_build_selection (build_profile_ppc.json に sys/kern/exec/absent/exec_gfx.c が無く gfx_emergency.c が在る)。QEMU PowerMac: 面を作って落ちるアプリの後で面の数が元に戻る。gfx2_exec_host.c は自前の stub で lease → release → poison しか見ないので、この項目の根拠にしない | hooks.mk の HOOK_gfx を PC98_PENDING に戻す (test_build_selection が赤) |
| 配り手 → 機種フック → 所有の返却の順、フックの時点で所有者がまだアプリ | host (新規 exec_gfx_exit_order_host.c): trident_emergency_host.c  の土台 の形で exec_finish.inc を組み、3 つの stub が呼ばれた順と、フックの中で読んだ appslot_gfx_owner() を照合 | exec_finish.inc で exec_gfx_owner_exit を appslot_gfx_owner_exit の後ろへ |
| gfx_shutdown の後に CUI の文字が戻る (PowerMac) | host: fbcon_host に park → 書き込み → retarget → 描いたセルの照合。QEMU PowerMac: gfx_init → 描画 → gfx_shutdown、および CUI から gfx_shutdown だけを呼んだ後の画面の文字とシェルの入力 | console_return の呼び出しを gfx_core.c から消す |
| 止めている間の出力を失わない | host: fbcon_host で park 中の putchar が retarget の後に出る | fbcon_park で cells を放す |
| shutdown 忘れ・異常終了でも CUI に戻る (PowerMac) | QEMU PowerMac: gfx_init の後で fault するアプリ、kill、の 2 通り。シリアルに「FB text ready generation=」が再び出る | exec_finish.inc の exec_gfx_owner_exit の行を消す |
| 所有者でないアプリの終了では復帰しない | host: console_text.c を stub の土台で組み、所有者違いの id で gfx_shutdown が呼ばれない | owner_exit の id の比較を消す |
| つないでいない端末を止めない | host: fbcon_host で attach の前の park が 0 を返し cells が無いまま。起動の列で text_state が attach の後に初めて 1 | fbcon_park の ready の早戻りを消す |
| 復帰の 2 度呼びは無害 | host: console_return を続けて 2 回、世代が 1 回しか進まない | text_ready の早戻りを消す |
| PC-98 は変わらない | build_compare (差 0)、CI の check・build。NP21/W は次の節目に相乗り: gfx_init → gfx_shutdown のアプリと GUI の出入り (os32gui → 終了 → CUI) の画面 | platform/pc98 の inline に display_restore_98() を入れる (CUI の画面が変わる) |
| PPC の像が組める | PPC のリンク (--no-undefined) と test_kapi_binding.py (旧版 3baaeeee4857) | 公開表に gfx_init_200 を足す (一覧の検査が赤) |

## 7. 保留の 2 件の扱い (この追記では決めない)

| 件 | どの段・どの口 | 理由 |
|---|---|---|
| mode_set・flip・present | present は段 5a-ii・5b (共通の present が後端へ渡す)、mode_set・flip は段 5c の「表示 ops」(親設計の順の最後)。口は既存の gfx2 の adapter の ops (of_fb_v2) | この追記の口 (門・復帰・回収) はモードを変えない。PPC_FBCON_DESIGN.md の範囲 (同じ形の attach、同期の flip) を越える分は hw_mode の件の結論を待つ。保留の出所の文書を確かめられなかった (食い違い 7) ので、5c の設計の往復で出所を付けて扱う |
| check_op | 段 5c の「caps・check_op」の所。sys/hal/gfx/v2/check_op.c は共通の `check(a, &ex, how, 1)` に渡すだけで、SW と答える規則はその中 (この追記では読んでいない) | 規則を変えると x86 の KAPI の答えも変わり得る。P6 で公開する 10 本に check_op は入れない (test_kapi_binding.py (旧版 3baaeeee4857) の「無いはず」に残す)。submit を公開する前に 5c で決めるなら、P6 は面・lock だけ先、submit・fence は 5c の後に割る |

## 8. 未決 (ユーザー判断が要るもの)

無し。r1 の 2 件は PM 決定で閉じた:

1. PowerMac だけ shutdown 忘れ・異常終了で自動で CUI に戻す — 推奨どおり (PC-98 は変わらない)。
2. check_op の規則 — 段 5c へ送る。x86 の KAPI の答えが変わる案が出た時だけユーザーへ。それまで PowerPC は check_op を公開しない。

## 9. 反論

無し (A1〜A12 は全部受けた)。補足 1 件: A8 の PowerMac の側は v1 の描画 KAPI の門を読んでいないので推測のまま残し、段 5b の受入に送った。

確定 (2026-11-19、Opus 5.5 (reviewer) の第 2 回 Approve、PM が反映): B1 H7 の完了条件は「exec_notify_owned を組む・切り出す土台 (test_exec_r1.py (旧版 b50e9720332b) の exec_gfx_return_emergency を自前で与える所、trident_emergency_host.c (旧版 63e167366ef8) の同じ所) に exec_gfx_owner_exit の stub を足す。要否は grep で exec_notify_owned と exec_finish.inc を見て決める」。B2 H6 の完了条件に「sys/kern/exec/exec.c を include する host の試験 (grep -l 'sys/kern/exec/exec.c' 旧ホスト試験) が組めること」。
