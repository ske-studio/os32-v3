# GF_STAGES_DESIGN — GeForce の段分け (書き直さず、既存コードと試験を移す) — 確定設計

> 状態: **現行 (2026-10-29 確定)** — astra 起草 (r1〜r3)、Opus のレビュー 3 往復 (r1 要修正・r2 要修正・r3 確定可)、PM の追記 §10。往復の記録は `~/os32-tmp/run/gf-stages/`。票は [TASK_GF_STAGES](../../tasks/realhw/TASK_GF_STAGES.md)。
> 実行主体: Codex。正確なモデル ID は環境から確認できないため未確認（依頼の指定は `gpt-6-astra`）。

根拠の相対パスは `/home/hight/os32-v3/`、設計基点は `6dc8a284`。読取り時HEADは `f4cf3dfb`、確認対象のkdebug/hwdev・monitor・C/CLI試験は基点との差分なし。
`D/` = `docs/decisions/realhw/`、`T/` = `docs/tasks/realhw/`、`H/` = `tools/tests/`。
裸の `recover*.c`／`gfinit*.c/.inc` は `kdebug/` 以下。`ROLES` は `docs/tasks/agents/ROLES.md`。
`N/` = `/home/hight/os32-tmp/refs/geforce6200/nouveau/drivers/gpu/drm/nouveau/`。手本は取得済み c03c1a1 の MIT 部（`N/dispnv04/hw.c:6`）。
本票の新しい名前・行数・時間定数は設計案／見積り（推測）。ユーザー・PM決定の根拠は同じディレクトリの `BRIEF.md`・`BRIEF_r2.md`・`BRIEF_r3.md`。本票だけで移行を読める。

## 1. 目的と範囲

OS32 は単一利用者・単一タスク・協調切替。攻撃者は想定せず、ハードの誤動作とバグに対して、固まらず、壊さず、98 に戻せることを守る。
**決定**：A1／初期化（A2＋main 0〜4）／C／戻し／調べを、今のコードの責務に沿って分ける。旧一式を凍結して新一式を並べない。
まず「main 4 → 同じセッションで C → abort/S0 → C だけ」を実機で確かめる。成立前に大規模な分割や別ローダーを作らない。
常駐状態は kdebug に1個。C の都度ロードは作らず、現行の `kdebug.bin` と起動中 pin される `hwdev.mod` を使う（`build/hwdev.mk:1`, `kernel/hwdev_module.c:123`）。
今回分ける実行単位は段の入口・ソース・ホスト試験。5個の配備バイナリへ変える仕事は含めない。コード変更は従来の再配備、照合変更は実行時指定で扱う。
新規は再Cの入口・常駐値・record・既存列への薄い接続の数百行を目標とする。行数は強制せず、必要な処理を削ったり詰め書きしたりしない。
本番ドライバは作らないが、modeset の列は本番も呼べる同一ソースにする。GUI/HAL接続、他mode、1850=1比較、同時実行、権限分離、新しい失敗分類は足さない。
初期化・A4・VPLL診断・virtual 等が使う計画実行器は維持する。新Cのための汎用実行器、予算解析、S0予約枠、全段hash固定は作らない。

## 2. 移す先の地図と機械的な分割

`recover.c` は現状2,423行。**分割だけの差分**では関数本体・static・定義順・コンパイル条件を保存する（`docs/POLICY_DEV.md:127`）。
独立 `.c` 化でinlineやIPAが変わるのを避け、まず以下の `.inc` を元の位置で include する。同じ翻訳単位のままソースを小さくし、実体は1個だけにする。
次表の範囲は基点での抽出対象。端の関数名を含む範囲内の**全関数定義と付随する宣言・定数**が指定先に属する。細切れの関数単位ファイルは作らない。

| 元 `kdebug/recover.c` の関数範囲（対応表） | 移す先（`kdebug/` 以下）・責務 |
|---|---|
| `discard`:232 〜 `transport_state`:449（485行まで）。`rx_word`, `recover_receive`, `receive_boundary`, `row_text/number/start/send`, `recover_record`, `recover_gf_pre/result`, `gf_bus_record`, `mode_vram_record`, `transport_diag` | `recover_common/log.inc`：受信・REC。GFの記録書式もここで1回だけ持つ。 |
| `rule_by_id`:556 〜 `profile_ok`:834（844行まで）。`expr_by_id`, `layout_for`, `is_bar`, `recover_target_address`, `value_ok`, `zero_target`, `n1_target`, `saved_target`, `index_save_safe`, `recover_profile_ok` | `recover_plan/profile.inc`：既存記述子の検査・番地変換。 |
| `account`:845 〜 `recover_delay_ms`:958（973行まで）。`restore_time`, `recover_restore_point`, `tx_point/time`, `recover_uart_byte/flush` | `recover_common/clock.inc`：有限待ちと安全点。既存経路の予算処理は移動するだけ。 |
| `log_phase`:974 〜 `cleanup`:1120（1194行まで）。`logged_read/write/callback`, `route_call`, `logged_route`, `config_ok`, `write_verify`, `quiesce`, `screen` | `recover_common/s0.inc`：局所後始末・停止・98復帰、共用する記録付きI/O。 |
| `live_matches`:1195 〜 `recover_request`:1430（1439行まで）。`check_deadline`, `pause_call`, `trial_begin`, `call_point`, `recover_safe_point`, `trial_step/end` | `recover_common/session.inc`：単一ownerの開始・中断・終端。 |
| `bit_get`:1440 〜 `release_plan`:1805（1815行まで）。`bit_set`, `dependencies`, `select_record`, `skip_head_change`, `node_rule`, `arg_ok`, `node_ok`, `plan_crc`, `recover_bind_plan` | `recover_plan/bind.inc`：計画の検査・結合。 |
| `extent_ok`:1816 〜 `recover_access`:2152（2155行まで）。`raw_matches`, `bus_address/read`, `zero_guards`, `guards`, `capture_read`, `evaluate`, `access_error/phase`, `cr1f_data`, `saved_write_value`, `readback_value`, `access_impl` | `recover_plan/access.inc`：計画のアクセス・capture。 |
| `mode_vram_access`:2156 〜 `mode_wait_access`:2309（2325行まで）。`mode_color_record/access`, `mode_check_record`, `recover_mode_route_active`, `mode_route_access`, `mode_check_access` | `recover_gf_stage/c.inc`：C固有。planによるVRAM・color・CHECK・route・waitを維持して移す。 |
| `recover_plan_access`:2326 〜 `recover_plan_leave`:2377（2396行まで）。`recover_plan_check/enter` | `recover_plan/step.inc`：計画の一歩。 |
| 上記以外：`recover_diag_get/fn/where`, `tx_fail/wait_fail`, `crc_word`, `recover_profile_crc`, `snapshot_crc`, `same_identity`, `token_matches`, `fail`, `recover_owner/configure`, `recover_route_test_session`, `recover_now_session`, `state`, `recover_init`, `finite`, `width_ok/mask`, `range_ok`, `recover_bar_probe` | `recover.c` に残す。owner・共通定義・入口・includeの束、基点換算で約340行。 |

分割の道具は `/usr/bin/clang -Xclang -ast-dump=json -fsyntax-only`。実装時の32bit target・include・defineを渡し、主ファイルの関数定義の開始／終了位置を得る。
小さな `tools/split_c_functions.py`（80〜120行目標）が上表の関数→移動先を展開し、元の行範囲を切り出す。コメントは関数直前から直前の空行まで含める。
人が決めるのは対応表。行番号を手でコピーせず、宣言・条件コンパイル・空白を含めて全原文が一度だけ残るよう区間を分配し、元位置を include に置換する。
T1/T2後に行番号がずれても関数名で対応させ、消えた関数は削除台帳で確認する。ASTに現れない `#if` 側も消さず、通常版と `GF_VPLL_PROBE` 版の区間を合わせる。
同じtoolchain/flagsで分割直前・直後をコンパイルし、`objdump -dr` で関数ごとの命令byte・参照先を比較する。番地表示／デバッグ行番号だけを比較対象外とし、再配置はsymbol＋addendも照合する。
inlineで独立symbolがない関数は呼出先へ展開された命令を含め、`.text` 全体でも照合する。差があれば分割を直す。最適化を両側で落として一致を作らない。
この検査をrecordやC列変更の差分に混ぜない。ctagsは未導入なので採らず、C++向けclang-move・書換え用coccinelleも使わない（依頼の決定）。

| 他の現行ファイル・関数 | 行き先／残す理由 |
|---|---|
| `kdebug/kdebug_window.c:50` `recover_window_cleanup`、`:90` `recover_window_access`；`recover_profile.c:42` `recover_cell_98`、`:57` `recover_screen_98` | その場で共用。窓・記録・98復帰を複製しない。Tridentにも共用可、GF停止列はGFだけ。 |
| `kdebug/recover_gf.c:125` `gf_quiesce`、`:406` A1 plan、`:447` A2 plan；`gfinit_bus.c:25` raw config、`:66` bus access、`:139` local cleanup | 451行／211行で既に責務がまとまるので移動不要。局所復元は毎回の値を使う。 |
| `kdebug/gfinit_plan.c:86` `gfinit_plan_prefix`、`gfinit_decode.c:653` `gfinit_decode_rom`、`gfinit_exec.c:80` `gfinit_exec_run` | 初期化でそのまま使用。decoder 756行も今回の分割目的だけで動かさない。A4は`:133`の既存builderを残す。 |
| `kdebug/gfinit.c:448` main成功、`:338` `gf_modeset`、`:211` `gf_release_rom` | **新規**の値引渡しとC再開をここへ結線。新しいinit owner/contextは作らない。常駐値は新 `kdebug/gf_stage_state.c/.h`。 |
| `gfinit_plan_c.c:203` `c_fields`、`:216` `c_check_out`、`:411` VRAM、`:442` color；`gfinit_mode.c:37` mode計算 | Cの保存値・CHECK・既存ループとbuilderを `kdebug/gf_stage_c.c` へ移す／共有列へ接続。node/rule/layout/captureの生成は残す。mode/pll計算本体はその場で共用。 |
| `tools/gfinit_mode_tables.py:29` `initializers`、`kdebug/generated/recover_gf_c_tables.inc:1`；`gfinit_plan_c.c:314`以降の列走査 | T2で唯一の列を `hwdev/generated/gf_mode_rows.inc` に移す。既存Pythonを生成元とし、builderは共有列からC planを生成する。旧C表の別生成はやめる。 |

共有列はQ→P→S/D→PLL→拡張CR→palette/VGA→commit。値・順・FP条件・50/10ms待ちの正典を1本にし、C builderの列走査（`gfinit_plan_c.c:314`）をこの列の消費側にする。
**決定**：Cのplanを残す。既存builderのnode/rule/layout/capture生成と `gf_modeset` → `recover_plan_access` を流用し、列の別実装を除く。固定の小plan＋callback案は採らない。
本番の `gf_load_state` は将来この共有列を既存busの読書き・待ちで実行する形とし、本番driverも直接I/O用の新実行器も今回作らない。LIVEのRMW・FP条件は実行時のまま保つ。
試験CはP後のVRAM模様、前後CHECK、98セル、route01、RECをbuilderで付加する。paletteはmode入力、8色は既存mode側に残す。共有列は試験模様・REC・route・98セルを要求しない。
手本は `N/dispnv04/crtc.c:708` prepare、`:731` commit、`N/dispnv04/hw.c:827` load_state。新レジスタ列を推測で追加しない。

## 3. C だけの再実行の口

常駐値は起動中だけ有効。main 4の成功直後、最初のQより前に一度採る。再起動・新しい初期化開始で無効にし、ファイルから復元しない。
全値の採取成功時だけinit完了と基準値validを立てる。採取に失敗したら公開せずS0へ進む。追加の値領域は数百byte程度（推測）。既存作業域の常駐量は別で、再確保・複製しない。
保存するのはinit完了／基準値valid、identity、BAR3本のraw/base/length、ROM識別・VIDEO PLL limits、`c_fields` の8値、初回C040、PMC_ENABLE観測値、直前S0結果。
8値はCR1C/33/3C/4B/52・60081C・600850・680524（`gfinit_plan_c.c:203`）。CR52は毎回 `(初回s+4)&FF`、C040の復元先も初回値で固定する。
局所index/lock/窓の保存値はCの各回で採り、初期化基準値と混ぜない。**init完了時は `gfinit_call` 末尾（`gfinit.c:463–467`）の `gf_release_rom` を飛ばす**。
同じ条件を `gfinit_recover_end`（`gfinit.c:235`）にも適用し、既存ownerのcompiled・profile・bus・leaseとその参照先ROM/decoded/flowを常駐で保持する。値領域へpointerを複製しない。
再Cは保持した作業域でC planを組み直し、flow容量を再利用する。S0で旧planの結合を外してから再bindするが、裏の確保領域を解放しない（`recover.c:1805`）。
init完了の無効化時は再Cを拒否し、S0が参照するprofile/callbackを使い終えてから既存の解放手順へ戻す。再起動で全常駐値は消える。cold latchによる新initの拒否は緩めない。
入口案は `gfinit c-only --allow-write --verify record|required [--no-verify-file PATH]`。ゲストが新しい実行番号を採番し、毎回Qから実行する。途中nodeの再開ではない。
T1で `gfinit_plan_build_c` のprefix呼出し（`gfinit_plan_c.c:272`）とC追加部分を分ける。同一セッションの初回はprefix＋C、再Cは保存limitsと空の計画からCだけを組む。同じC追加関数を使う。
再Cのsaved式は常駐基準値からliteralへ解決し、初回CR52/C040を再採取で上書きしない。現在値が要るLIVEのRMW・FP条件は現在値を読み、基準値で置き換えない。
初期化ROMの再decode・A2/mainの再実行はしない。C-onlyでは開始前にID/BAR/範囲を確認し、旧snapshotに依存する入口BAR期待値とsaved operandsを常駐値から設定する。
C-onlyはcold latchで拒否する `gf_inspect/gf_begin`（`gfinit.c:242,361`）を呼ばず、同じownerの専用入口から新sidを得る。初期化の入口は変更しない。
例外条件Eは「init完了・基準値valid・C-only・再C許可・直前S0成功・ID/BAR等の入口検査成功」がすべて成立した場合だけ。3か所とも同じEを使い、failedを先に消して通さない。
`recover_owner_configure`（`recover.c:222`）、`recover_bind_plan`（`:1666`）、`trial_begin`（`:1238`）の**各入口でEのときだけ failed/cold_latched の拒否を迂回**し、終端 `REC_FAILED` も許す。RUNNING/PAUSEDからは開始しない。
それ以外のplan有無・結合・profile/CRC・identity/範囲の検査は維持する。**failedを消すのは `trial_begin` の既存の新session初期化（`:1316`）だけ**。cold_latchedは消さず、`:1327`でもC profileのcold_required=trueを維持する。
定数の期限を使うC実行種別を既存の開始・安全点・送信・終端に通す。`gf_budget`、`recover_budget_apply`、`cleanup_budget_compute/prepare` の予約計算はC-onlyから呼ばない。
`row_send`／`transport_state`／`restore_time`／`cleanup` までこの分岐を通し、旧予算をダミー値で満たす方式にしない。通常のinit/A4等の予算処理はそのまま残す。
T2以後もC planと既存bind/access/local保存・戻しを使う。構造検査・内部CRC/hashは残すが、hostや全段の固定契約にしない。新しいplanなしのバス経路は足さない。
新分岐にも同じ `cleanup` のlocal→quiesce→screenを使い、S0本体を別に書かない。送信が失敗しても記録の成功を前提にせず、独立に可能なS0へ進む。
入口は生ID/BAR一致、Command=0102、cfg50/54・1850/1854=0、route&3=0、BAR1範囲≧307200B、PMC_ENABLEが保存観測値と一致。表示中から直に重ねず先にS0を要求する。
これらは初期化履歴と限定した実値の確認であり、PFB・CR全状態の保持の証明ではない。**未確認**：S0後にもCに必要なPFB・CRの初期化が残るか。
依頼にある4回目の終端後kpeekではPLLとPMC_ENABLE=07110111が残り、1850/1854/PMC140は0だった。再C成立はここからの**推測**で、実機受入で判定する。
再実行を許すのは正常表示後・明示abort・機能CHECK不一致からS0が成功し、入口条件を再確認できた場合。バス／窓／S0失敗、ID/BAR変化、init失敗ではinit完了を無効にする。
timeout・送信失敗からの自動再Cは初版では行わず、既存のS0→再起動手順へ戻す。host解析だけの失敗でゲストのinit完了を消さない。新しいreason分類は不要。
**実機分岐**：再Cの①入口検査で拒否、②requiredのCHECK不一致、③映像が出ない／固まる、のいずれでも再Cを不成立とし入口を無効にする。固まれば物理リセット、ほかはS0の後、既存の再起動手順から「初期化と同じセッションでC」に戻す。PFB/CR修復や部分再生は足さない。
この場合もrecord・実行時指定・共有列・分割は進められる。再Cは未達として記録し、成立したかのように完了扱いしない。

## 4. 照合とS0

既定は `required`、初回台本では `record` を明示する。指定はCの開始ごとに受け取り、形式／対象の誤りは書込み前に返す。
現 `MMIO 680a24`／`CR 1c`／`SR 01`、16進、空行・#コメント、64指定の形式を継承する（`userland/shell/cmd_gfinit.c:21`）。実行中にファイルを再読しない。

| 対象 | record | required／除外ファイル |
|---|---|---|
| 入口ID/BAR/Command/legacy・窓/バス・範囲・98セル・route・S0 | 必ず検査し、失敗は停止／S0 | 同じ。除外不可。 |
| VRAM全域比較のデータ不一致 | 不一致の位置・expected・actualを記録して後続CHECKへ進む。範囲/BAR・読書き失敗は停止／S0 | 全域比較を実施し、不一致でS0。除外不可。 |
| mode機能の必須CHECK | 安全に読める実値・expected・mask・不一致を記録し、不一致だけでは止めない | 下記の最終時点で照合。不一致でS0。除外ファイルでも必須CHECK自体は残す。 |
| 書込み直後の任意照合、未知scratch・510・TEST sense | 記録のみ。副作用のある読取りは追加しない | 記録のみ。既存の除外指定も任意照合だけに効く。 |

必須CHECKの正典は `gfinit_plan_c.c:216` の内容を移す。50Cはmask EFFFFFFF／期待10000204、508は0007FFFF、578は8000FFFF、580 bit8、524=s&~50000、C040=初回値。
PLL係数は50C選択後。CONFIG&3=2、START=0、CR00..18（最終11=AB/17=C3）と19/1A/25/28/44/42/1B/20/47、CR33=s&~3B、SR01=01、MISC=E3、GENERAL=20100130。
CR1Aはmask D7、ほかの列挙CRはFF。DACCLK&101=1、TESTはbit20だけ。cfg50/54・1850/1854=0、Command0102、route&3=1、セル復元・VRAM一致も出口で確認する。
50C bit28、CR1A bit3/5、680A24の旧全bit照合は復活させない（`docs/POLICY_DEBUG.md:1585`）。DACの自動増分やARのFF操作を通常MMIOのreadbackにしない。
現 `gfinit_verify.inc:6` だけではrecordにならない。builderの `c_check_in`（`gfinit_plan_c.c:184–201`）が発行する全CHECK nodeに入口の印を付け、`mode_check_access`（`recover.c:2264`）へ渡す。
recordで不一致を非停止にするのは **印のない `REC_CHECK_TARGET` だけ**。入口TARGET（Command・cfg50/54・1850/1854・BARを含む）とIDENTITY/ROUTE/CELLは照合を続ける。読取り/送信失敗は印に関係なくS0へ進む。
印はnodeに付随する内部flag（`#define`）とし、builder・bind・契約生成を追従させる。CHECK番号やレジスタ番地で入口/出口を推定しない。requiredは印の有無によらず必須CHECKを照合する。
VRAMは `gfinit_plan_c.c:411` の `gfinit_plan_c_vram` の全域走査完了とデータ不一致を区別して `mode_vram_access`（`recover.c:2156`）へ返す。先に不一致があっても後のバス失敗を隠さず、完了した比較の不一致だけrecordで非停止にする。既存の失敗分類を使う。
S0は**local → GF quiesce（1850=0→1854=0→PMC140=0）→0FAChで98 → decode安全確認→98復帰・セル往復・再描画**。実機で通った順を維持（`recover.c:1135,1180`, `recover_gf.c:125`）。
生config不一致なら危険なMMIOを省き、可能な0FACh復帰は続行。legacy解放未確認なら98 VRAMに触れない。Commandは0102、BAR復元失敗ならdecodeを閉じたまま（`D/REALHW_RECOVER_DESIGN.md:81,135`）。
PLL/PFB/古いtimingはS0で戻さない。S0成功と完全冷間復元を同一視せず、既存PARTIAL／S1未保証の意味を保つ。無応答MMIO/CPU停止は従来の物理復帰へ（同`:78,120`）。

## 5. 試験も移し、全体固定を解く

既存caseを段ごとのhost入口へ移し、元caseは除く。fake bus/clock/transportだけを共通fixtureにし、別段の `_start` を改名して取り込む連鎖を解く。
直接 `recover.c` をincludeするのは `H/recover_host.c`・`recover_gf_host.c`・`recover_virtual_host.c`。さらに `gfinit_host.c:5` → `gfinit_exec_host.c:4` → `gfinit_bus_host.c:3` → `gfinit_plan_c_host.c:4` に波及する。
`recover_cli_host.c:11,36` はwireヘッダと `cmd_recover.c` をincludeするCLI試験で、recover本体の直接include一覧には含めない。
機械的分割の間は `recover.c` のinclude束を共通fixtureから一度だけ読む。最終的な段別試験は別の実行ファイルにし、他段のcase一式を実行しない。

| 今の試験／固定箇所 | 移動・置換後に確かめること |
|---|---|
| `recover_host.c`＋`recover_cleanup_host.inc`／`recover_t2_host.inc`／diag | 共通の窓・送信・S0試験へcase移動。local→quiesce→route、窓/BAR失敗、ログ失敗でも戻し、セル原値復元を保持。 |
| `recover_gf_host.c`、`gfinit_host/exec/fixture*.c` | A1測定・復元／A2・mainのdecodeと順序へ分ける。初期化の193命令等の証拠は残す。A4・VPLL・virtual・CLI回帰も使用中なので残す。 |
| `gfinit_plan_c_host.c`（993行）、`test_gfinit_plan_c.py:23` | `gf_c_sequence_host.c`／`gf_c_verify_host.c`／`gf_c_reentry_host.c` へcase移動。default_hash=81564298／default_crc=2834802941の固定は削除。独立期待trace・FP分岐・PLL順・mask・VRAM・paletteを検査。 |
| `gfinit_budget_host.c:216` の2298行、同`:163`のC/全体予算 | Cの行数・予算caseを除く。送信詰まり・abort・有限待ち・終端到達のcaseをC/共通試験へ移す。旧利用者の予算計算試験だけ残す。 |
| `test_dep_dir_a_l01/02/05a/08/09/10.py`（例l01`:70`、l02`:49`） | HWDEV契約/layout CRCの16進定数を6本から除く。各試験は自分のslot・型・境界・依存方向を固定。互換CRCは現在の生成契約を読み、故意に変えたCRCをloaderが拒否する反例を残す。 |
| `test_recover_tables.py:83`、`tools/gen_hwdev_contract.py:142` | 使用中の表の独立oracleと生成再現性は維持。HWDEVのABI/PAIR検査そのものは弱めず、CRC正典を生成器と契約試験1か所に集める。Cの私有値は新enumにしない（`docs/POLICY_DEV.md:150`）。 |
| `test_rshell_recover.py:410`以降のC再判定 | レジスタ期待値・66件固定を除き、連番／閉鎖／EOT／欠落とゲスト結果の転記を試験。壊れた保存JSON・invalid・CRC不一致でも戻しが送られる反例を残す。 |

変異の目印はcaseの移動先に追従させ、Cの文字列を依存方向試験から参照しない。`build/checks.d/` と `tools/check_map.d/` の入力を段のsource/include単位へ直す。
文字列検索をする既存試験と `tools/check_p2v_allow.txt` のfile:functionも移動先へ追従する。新state/core接続は正典定義から生成し、必要なABI更新までCRC固定撤去で隠さない。
新規caseは基準値の一度だけの採取、CR52非累積、PLL途中abort後のC040、S0失敗後の拒否、両終端で常駐参照を保持すること。期待値を製品列から生成して循環検査にしない。
failed/cold_latched＋REC_FAILEDからEで3入口を通り、E不成立・非Cは拒否する反例を含める。recordは入口TARGET/ROUTE/CELLの不一致で止まり、出口の印なしTARGETとVRAM不一致では後続CHECKが記録されること、VRAM途中のバス失敗は止まることを確かめる。

## 6. monitor と引数なしの戻し

`tools/rshell_recover.py` 1本を縮める。新parserを並べない。判定はseq、BEGIN/END、EOT、欠落とゲストOK/NGの写しだけ。
`MODE_C_CHECK_COUNT`／レジスタ期待表（`:47`）、再比較（`:304`）、`mode_c_success`（`:337`）を削る。必須CHECKの実施と集約結果はゲストが保証する。
recordは「観測完了」、required成功は「機械項目合格・映像保留」。S0のPARTIALをmonitorが独自にNGへ変えない。元のOK/NG行を改変せず報告に残す。
待ちは段別定数にする案：A1/A2/調べ240秒、main各900秒、C1800秒、戻し180秒。9600bps運用を基準とする推測値で、実機所要時間から定数だけ調整する。
ゲストCは送信・承認待ちを除く60秒、送信とS0にも定数締切を持つ。VRAMは行間・DACは色単位・delayは刻みごとの安全点を維持。byte/pollの予約枠や行数からの予算計算は追加しない。
待機中（shell受付可能時）は新しい `recover restore`（追加引数なし）を `--restore` から送る。常駐ownerが対象を選び、sid/state/image CRCは不要。既存 `recover now <sid>` は互換入口として残せる。
`main():550` のstate読込みより前にrestoreを分岐し、ログファイルを開けないことも送信の門にしない。返答解析が失敗しても戻し送信を取り消さない。
RUN中はshellを再入せず、道具が現ストリームからsidを補った**既存abort frameだけ**を送り、安全点でS0へ進む。sidを利用者に要求せず、固定S0 frameは追加しない。sidを取得できない通信断・停止は既存の期限／物理復帰手順の範囲とする。
戻しの状態は常駐1個。C-only終了後も保持中のprofile・bus・callbackでS0を呼べること、解放時は既存S0へ付け替えてから領域を返すことを試験する。都度ロードや二重ownerは不要。

## 7. 実装の段と受入

各段の「新規」は移動以外の製品行（変更して書き直す行も含む）。試験・分割道具は§8に別計上。これは実装時の手順であり、本起草では実行しない。
1,000行超のファイルを触る段はsol/astra、ほかはterra可（`docs/tasks/agents/ROLES.md:12`）。受入の実機操作はPMとユーザーの既存の段別立会いで行う。

| 段 | 内容・触る主ファイルと規模 | 新規製品行／実装者 | 受入 |
|---|---|---|---|
| **T1 最初に再Cを実測** | `gfinit.c`474行、`gfinit_plan_c.c`453行、`recover.c`2423行、新state、wire/CLI・既存HWDEV接続、monitor580行。§3のC-only、§4record、§6戻しを結ぶ。既存列は1本のまま。monitor再判定は120〜160行削除。 | **230〜330**／solかastra | hostで常駐保持/解放/lease・3入口のEと失敗出口、NP21/WでGF不在拒否と戻し入口。実機でmain4→C(record)→abort/S0→C-only(required)→目視→S0。A2/main実行番号が増えないこと。失敗なら§3の同一セッションCへ戻す。 |
| **T2 直後に共有列へ移し、旧列を消す** | `gfinit_plan_c.c`453行とC表320行、`recover.c`内C固有170行、`gfinit_budget.inc`103行、生成器・C試験。§2の列／CHECK／ループを移し、両入口のbuilderを共有列へ接続。C planと実行・局所復元は維持する。 | **70〜120**／solかastra | 独立traceと中断/復帰を確認→差替えた同じ列を実機でC→S0→再C（不成立分岐ではinit→C→S0）。**その受入直後、ほかの段を挟まず**旧列走査の置換済み部分・旧C表の重複・C予算計数・全体固定caseを削除。builder全体は消さない。製品廃止140〜210行、T1との累計260〜370行目標。 |
| **T3 recoverの機械的分割** | `recover.c`残存定義を§2の対応表で約2000行移動。include束、hostの実ソースincludeと入力登録を更新。新機能は混ぜない。 | **10〜20**／solかastra | 通常/診断版のコンパイル＋objdump一致。直接host、NP21/Wのvirtual/戻し/peek。機能変更が混じれば別差分へ戻す。 |
| **T4 試験の段別化と残りの結線整理** | `H/gfinit_plan_c_host.c`等から計2000〜3000行のcase/fixtureを移す。依存方向6本・変異目印・check登録、必要なwire/build接続・台本の残り。 | **0〜20**／terra可。生成表など1000行超に触れるならsol/astra | Cのmode値/照合変更でA1/init・依存方向の固定値更新が不要。ABI不一致拒否は保持。最終統合でA4/VPLL/virtual/peek/shell・部品不在/KDEBUG=0、実機C/S0・reboot比較を確認。 |

T2はT1受入の直後に必ず続ける削除段。T1不成立なら同一セッションCの受入を使い、再C用の不要な暫定コードも消す。削除を将来の別決裁へ送らない。
消すのは共有列への接続で置換された旧列走査・C専用予算計数・旧C表の重複記述・monitor二重判定と全体固定試験。node/rule/layout/capture構築とC実行器は残す。移した列・CHECK・builderを廃止へ二重計上しない。
`recover_gf_tables.inc`1215行は初期化等が使用中なので残す。A4/VPLL/virtualが使う汎用関数は消さない。旧Cを選ぶ実行時スイッチや凍結版一式は置かない。
T2の列移設は呼出しの変更を伴うので機械語一致の対象外。移動と書換えを分けて報告し、列の値・順は独立trace、映像とS0は実機で確かめる。
実装者は各段のbuildと直接試験MUTATE=0・check登録lintを実施。最後に全体buildと変更全体の候補・規定静的検査を1回、PMが取り込み時の変異確認を行う（ROLES`:17`）。
実機合格は白枠四辺・8帯・市松・色/位置/静止と、98へ戻した後の入力更新を含む（`tools/accept/gf_c.md:24,26`）。NP21/WをGF映像やS0後の初期化保持の証拠にしない。

## 8. 新規・移動・削除の合計

以下は基点からの差分見積り（推測）。移動Mは元からM行削り先へM行置くため純増0。修正して書き直した部分は新規N／旧行削除Dに計上する。

| 区分 | 新規N | 移動M（両側を足さない） | 廃止D（移動元を含めない） | 最終純増 N−D |
|---|---:|---:|---:|---:|
| 製品C/ヘッダ・生成表・既存monitor/結線 | 310〜490 | 約2400〜2700 | 260〜370 | **−60〜＋230** |
| host試験・試験登録 | 150〜250 | 約2000〜3000 | 400〜600 | **−450〜−150** |
| AST切出し道具 | 80〜120 | 0 | 0 | **＋80〜120** |
| **合計** | **540〜860** | **約4400〜5700** | **660〜970** | **−430〜＋200** |

製品NはT1 230〜330＋T2 70〜120＋T3 10〜20＋T4 0〜20。T2はplan廃止・直接実行器新設をやめ、常駐再構成も不要になった前提で積み直した。
製品DはT1 monitor 120〜160＋T2 140〜210。T2内訳は旧列走査の接続置換部分20〜40（`gfinit_plan_c.c:314–385`内、node構築を維持する部分は移動のみ）、C予算計数80〜100（`gfinit_budget.inc:6–103`）、旧表の重複40〜70（320行中、共有列への移動分を除く）。
C専用予算計数は同一sessionのCも定数期限へ結んでから除く。init/A4等の予算と汎用関数は残す。builderの維持部分・C実行器・移した列/表はDに数えない。
範囲の純増は最小N−最大D〜最大N−最小Dで計算。全体は最大＋200行（約1%）もあり、純減を保証しない。一式複製はなく、plan維持で削れない分を数字から隠さない。
受入前の一時的な増加は廃止をまだ引かない上限で製品490行、試験/道具込み860行。T2受入直後に上記廃止を続け、T1だけで長期停止しない。
元の規模は製品手書き約7700＋表1535、試験8526行（`T/TASK_GF_STAGES.md:16`、表は現物計数）。見積りは圧縮のノルマではなく、移動率と重複撤去を確認するための数字。
PMは各段でN/M/Dを実数に置換する。製品新規490行または全体純増200行を超える見込みなら不足する接続を示し、必要な頑丈さを削らず、旧新の二重実装になっていないかを先に直す。

## 9. ユーザーに決めてもらう所

**無し。** 再Cが成立しない場合の同一セッションへの後退も決定済み。未確認のハード状態はT1の観測で分岐し、新しい初期化修復はこの票に加えない。
今回行ったのは資料・コードの読取りと本票の作成だけ。リポジトリへの書込み、実装、ビルド、試験、NP21/W、実機操作は行っていない。

## 10. PM の追記 (2026-10-29、Opus の 3 往復目のレビュー opus_r3 の N1〜N3、確定可)

- **N1 (restore の後も init を保つ)**: 手順 main4 → C → abort (S0) → 待機中に `recover restore` → `gfinit c-only` で、restore が今の S0 だけの経路 (recover.c:514 の ++next_session) に倣って新しい sid を取ると、gf_lease.session ≠ token.session で gf_owned が偽になり、gfinit_call (gfinit.c:425-427) が gf_release_rom を呼んで init が消え、cold latch で再起動まで C だけの再実行ができなくなる。→ **restore の後も GF の owner と lease を保つ** (init を持っている間は gfinit.c:425 で解放しない、または lease.session を restore の sid に追従させる — T1 の実装で小さい方を選ぶ)。restore の S0 も §3 の E の「直前の S0 成功」に数える。§5 にこの手順の host case を 1 つ足す。
- **N2 (同じセッションの C の期限)**: 同じセッションの C は init の prefix と C を 1 つの log_limit/max_polls で数えている (gfinit.c:292-310、gfinit_budget.inc:31-90)。T2 で C の予算計数を除くときは、**c_node に入った所でセッションの期限を C の定数に替え、S0 の予約は保つ**。切替えの行数は T2 の新規 N に入れる (数行の見込み)。切り替えずに gfinit_c_rows を消すと C の行が上限に入らず打ち切られる。
- **N3 (古い .d)**: T2・T4 で表を kdebug/generated から hwdev/generated に移す・gfinit_plan_c.c を移して消すときは、増分ビルドの古い .d が消えたファイルを指して `make all` が止まる。依頼に「古い .d を消して組み直す」を書く。
- 小さな点: §3 の gfinit_recover_end の行は :235 ではなく :232。
