# TASK_GF_DRIVER — GeForce 6200 の本番の画面ドライバ (1 機能 1 ファイル + 抽象化の層 + OS32 の HAL)

> 上位: [実機の票の索引](PLAN.md)

> 状態: **受入完了 (2026-10-30)** — **P0〜P6 は main、実機 Ra266 で GUI が GeForce の本番ドライバで動くことをシリアルと目視 (5 場面) で確認**。**P0〜P5 と P6 の後片付けは main** (P4+P5 は 68c33de6、P6c は 040e996e: 診断の A2/main を本番の runner に寄せ旧い展開を消した、実装 −500 行、astra 取り込み可。`gfxmode geforce` は f345ea8c)。**P6 の実機 (2026-10-30、Ra266、68c33de6、GFX=geforce・GUI=0): シリアルだけの確認は合格** — `[gfx] init gen=2 be=geforce started=1`・最初の present 307200 bytes・gshell の準備完了、60 秒後に CUI へ戻り `ver` が「GFX: GeForce 640x480x8 bpp」、kpeek で 50C=00000204・508=00021A07・578=8000011F・PMC200=07110111・1850/1854/140=0。**P6c 後 (4e96d02d、2026-10-30 07:10) もシリアルの確認は同じく合格** (ノートのセッションが実施: `os32gui -t 60` で `[gfx] init gen=2 be=geforce started=1`・first present 307200、60 秒後に CUI、ver「GFX: GeForce 640x480x8」、レジスタは 68c33de6 と同じ値、diag の失敗 0、証拠はノートの `~/os32-tmp/evidence/2026-10-30/realhw-4e96d02d/p6_gui.txt`。GeForce の初期化は GUI を起こしたときに行われ、起動直後の ver は 98 のまま — 正常)。**持越しだった 4038h・403Ch の退避・復元は実機で害なし** (初期化の完走と戻しで異常なし)。**画面の目視 (2026-10-30、ユーザー立会い、4e96d02d、写真 2 枚)**: (0) GUI のデスクトップが 640×480 で端まで出る・色 (デスクトップ・タスクバー・時計) がパレットどおり・カーソル正常、(1) マウスを動かしてゴミ無し、(2) Run のダイアログの枠・文字・記号・入力が描ける — 文字が「若干汚いが読める」のは液晶 (LCD1770VM、1280×1024) の非整数の拡大・位相の見込み (未確定、モニタの自動調整か PEGC との見比べで切り分ける)。(3) GUI を終えて CUI へ戻り 98 のテキスト画面が出てプロンプトに入力できる、(4) 2 回目の GUI も同じ画面 — **P6 の目視は 5 場面とも合格**。文字のにじみは**モニタの自動調整で解消** (液晶のアナログ入力の位相、ドライバ側の問題ではない。モニタの表示は D-SUB 31kHz/59.6Hz 640×480 — 標準の 59.94Hz より低いのは PLL 00021A07 が 27MHz×26/(7×4)≈25.07MHz で 25.175MHz より 0.4% 低いため、許容の内)。送り項目: 実機の /etc/system.cfg は **GFX=geforce のまま使う** (ユーザー決定 2026-10-30「geforceのままで」。元の設定は /etc/system.cfg.p6bak に GFX=pegc として残る) — それまで実機の /etc/system.cfg は GFX=geforce のまま (元は /etc/system.cfg.p6bak、GFX=pegc)。自己試験の合格数 1813→1807 は常駐の像が 2 頁伸びて slack_cycle が 2 頁×3 件減った分 (不具合ではない、前例は TASK_DEP_DIR)。それまで: 設計は確定 ([GF_DRIVER_DESIGN](../../design/hwdev/gf/README.md))、**PM が詳細な実装計画を説明しユーザーが了承** (2026-10-29「ok」)。P0 (取り決め、sol) と P1 (機械的な分割、PM の Opus サブエージェント) から。初期化の命令の解読の分割は main。 **P0・P1・P2 は main** (2026-10-29): P2 は 27 命令を terra 27 本に同時に出し 2 分で全部 rc=0、sol がまとめて実 ROM の 193 命令で旧い展開と I/O 447 件が一致 (astra Approve)。**持越し (P4 の実機の受入で確かめる)**: PLL の退避で足した 4038h・403Ch の読取り・復元は NV44A で無害・全ビット復元可能かの根拠が nouveau に無い (hwdev/gf/pll.c:33、astra の所見)。

## 0. ユーザーの提案と決定 (2026-10-29)

- 背景: GeForce は 2026-10-29 に映像を出したが、あるのは実機の試験・調べの道具 (kdebug の `gfinit`・`recover`) だけ。OS32 の画面 (GUI・コンソール) を GeForce に描く**本番の画面ドライバは未実装**。
- 提案 (原文): 「BIOS での機能は多分 100、200 近くぐらいあると思うのですが、それぞれのその BIOS の機能に対して 1 機能 1 ファイルで生成し、それを抽象化する層を一段設けて実装し、その次に OS32 の HAL の部分としての実装をする方法はどうでしょうか。これなら並列で処理をできるのではないでしょうか。」
- 「BIOS の機能」は **両方を合わせたもの** — (A) ROM の初期化スクリプト (devinit) の命令 (Ra266 の実 ROM で 27 種・193 命令) と、(B) VGA BIOS のサービス (INT 10h・VESA VBE に当たる、モード設定・パレット・表示開始の番地・blank など)。
- 「基本的に推奨の方法で」「**既に実装済みのものは機械的に分割してから**行って欲しい」「機械的な下処理は Opus 自身がサブエージェントで行ってもよい」。
- 2026-10-28 の決定 3 (本番は nouveau の load_state に近い簡素な作り) と、2026-10-29 の反省会 (POLICY_DEV §6「実機の試験の作り」、段ごとの小さなソース) を引き継ぐ。

## 1. 形 (PM の推奨、設計の往復で詰める)

1. **下の層 (1 機能 1 ファイル)**: (A) 初期化の命令を 1 命令 1 ファイル。(B) レジスタの操作の単位 (PLL の設定・CRTC の時間の値・パレット・DAC と出力先・FIFO・カーソル・blank・表示開始の番地など) を 1 つずつ。共通部分 (レジスタの読み書き・待ち) は include。1 本は数十〜200 行で、terra に並列で出せる大きさ (行数は目標で強制ではない)。
2. **抽象化の層**: VBE に近い少数のサービス (モード一覧・モード設定・パレット・表示開始・blank/unblank・元に戻す)。下の層の組合せで書く。GF_STAGES の T2 の共有の列 (`gf_load_state`) は「モード設定」の中身になる。Trident も同じ形の口に載せられるようにする (下の層を差し替える)。
3. **OS32 の層 (HAL のバックエンド)**: 抽象化の層だけを呼ぶ。`gfx_present`・フレームバッファの写像・0FACh の切り替え (起動・終了・障害で 98 へ戻す)・部品の置き場 (kdebug の診断から本番の部品へ)。
4. **並列化**: 抽象化の層の取り決め (関数の形) を先に決め、下の層の数十本を互いに独立に作る。試験も 1 機能 1 本。

## 2. 制約

- **手本は nouveau (MIT)** — 各機能の file:line を添える ([HW3])。機能の一覧 (B) は VESA の公開の規格から取る。**ROM の x86 のコード (VGA BIOS の実装) を逆アセンブル・解析して写さない** ([HW3]: 解析を禁じる条件の物は解析しない)。ROM の初期化スクリプトの表を読んで解釈するのは nouveau と同じで可。
- 既に実装済みのもの (初期化の解読・実行・PLL・モードの計算・資源) は、**先に機械的に分割**する (POLICY_DEV §2「分割の手段」: clang の AST で範囲、対応表、スクリプトで切り出し、objdump で機械語の一致)。下処理は PM の Opus のサブエージェントが行ってよい。
- 行数は目標で強制ではない。膨らみは PM が管理する (TASK_GF_STAGES §4 と同じ)。

## 3. 進め方

1. 既存の実装の機械的な分割 (Opus のサブエージェント、作業木): まず `kdebug/gfinit_decode.c` (初期化の命令の解読、756 行) を命令ごとの `.inc` に。GF_STAGES の T2 (sol、実装中) と触るファイルが重ならない所から。
2. 設計票 (astra 起草・Opus レビュー): 抽象化の層の取り決め、下の層の機能の一覧 (A の 27 種 + B の一覧) と各 nouveau の対応、OS32 の層、段の分解 (並列の割当て)。
2a. **実装の前に、PM が詳細な実装計画をユーザーに説明し、了承を得る** (ユーザー指示 2026-10-29「実装前には詳細な実装計画の説明をしてください」): 機能の一覧 (A・B) と各機能のファイル・nouveau の対応、抽象化の層の関数の形、OS32 の層の結線、段の順序と並列の割当て (terra・sol・astra)、各段の試験と受入 (ホスト・NP21/W・実機)、行数の見込み (目標)、実機で要ること。了承の前に実装を出さない。
3. 下の層を並列で実装 (terra 中心)、抽象化の層、OS32 の層、NP21/W (GF 無しの拒否) と実機の受入。

## 記録: GF_DRIVER_DESIGN (2026-10-29 確定) の計画・決裁・見積り

以下は当時の計画・決裁・見積りの記録であり、現状は冒頭の状態行を参照。

- 元 1 行: # GF_DRIVER_DESIGN — GeForce 6200 の本番の画面ドライバ — 確定設計
- 元 3 行: > 状態: **現行 (2026-10-29 確定)** — astra 起草 (r1〜r3)、Opus のレビュー 3 往復 (r1 要修正・r2 要修正・r3 確定可、R1 の文言は PM が直した)。往復の記録は `~/os32-tmp/run/gf-driver/`。票は TASK_GF_DRIVER (docs/tasks/realhw/)。**実装の前に PM がユーザーに詳細な実装計画を説明し、了承を得る。**
- 元 4 行: > 起草主体: Codex（GPT-6 系）。詳細モデル ID は環境から確認できず未確認。依頼の指定は `gpt-6-astra`。
- 元 11 行: **決定**は `T/TASK_GF_DRIVER.md:7–32`・確定設計・`BRIEF_r2.md:2` の PM 決定（`opus_r1.md:2–15` の全指摘採用）から引き継ぎ、`BRIEF_r3.md:2` の N1〜N5 全採用を反映する。以下の新しい関数名・配分・所要時間・行数は実装案／見積り（推測）。
- 元 16 行: **決定**：実装済み部分を機械的に分割してから変更する。中層と部品間の契約を先に固め、独立した小ファイルを terra 中心に並列実装する。
- 20: 表示は **640×480、PACKED8、256 色、pitch=640、表示先頭=0、25.175 MHz 要求**の 1 モードとする。既存 C の定数・計算を移す（`O/hwdev/gfinit_mode.h:6–20`、U1 決定）。
- 元 32 行: 下表の短い `.inc` は `O/kdebug/gfinit_op/` 以下、`decode.c` は `O/kdebug/gfinit_decode.c`。D/E は decode/expand。省略した E が本体に残る行は本体位置を併記した。
- 33: 最終の部品は `hwdev/gf/op/<命令名の小文字>.c` の **run だけ 27 本、各 5〜30 行、計 300〜500 行目標**。共通計算は共有する。下表 E は移行元で、P6 で診断 A2/main も同じ runner に移し、静的展開を削除する（F4）。
- 37行: `io_restrict_decode.inc:1` / `io_restrict_expand.inc:1`
- 38行: `repeat_decode.inc:1` / `decode.c:89`、`gfinit_decode/schedule.inc:36`
- 39行: `decode.c:24` / `decode.c:90`
- 40行: `copy_decode.inc:1` / `copy_expand.inc:1`
- 41行: `decode.c:24` / `decode.c:91`
- 42行: `condition_decode.inc:1` / `condition_expand.inc:1`
- 43行: `io_restrict_decode.inc:1` / `io_restrict_expand.inc:1`、`pll_expand.inc:1`
- 44行: `pll2_zm_reg_decode.inc:1` / `decode.c:99`、`pll_expand.inc:1`
- 45行: `cr_index_address_latched_decode.inc:1` / 同 `_expand.inc:1`
- 46行: `cr_decode.inc:1` / `cr_index_io_expand.inc:1`
- 47行: `cr_decode.inc:1` / `cr_index_io_expand.inc:1`
- 48行: `decode.c:21` / `zm_cr_group_expand.inc:1`
- 49行: `zm_reg_sequence_decode.inc:1` / 同 `_expand.inc:1`
- 50行: `decode.c:48` / `decode.c:95`、`gfinit_decode/schedule.inc:28`
- 51行: `copy_decode.inc:1` / `copy_expand.inc:1`
- 52行: `index_io_decode.inc:1` / `cr_index_io_expand.inc:1`
- 53行: `decode.c:24` / `decode.c:93`
- 54行: `reset_nv_reg_decode.inc:1` / `reset_expand.inc:1`
- 55行: `sub_decode.inc:1` / `decode.c:96`、`gfinit_decode/schedule.inc:28`
- 56行: `reset_nv_reg_decode.inc:1` / `decode.c:100`
- 57行: `condition_decode.inc:1` / `decode.c:101`
- 58行: `decode.c:24` / `decode.c:93`
- 59行: `decode.c:24` / `decode.c:92`
- 60行: `decode.c:53` / `decode.c:94`
- 61行: `condition_decode.inc:1` / `condition_expand.inc:1`
- 62行: `index_io_decode.inc:1` / `cr_index_io_expand.inc:1`
- 63行: `pll2_zm_reg_decode.inc:1` / `decode.c:101`
- 元 67 行: OS32 固有差分も保持：PMC_ENABLE bit0 を立てる、RESET 後 1850 は 0、CR3C の組を確認する。根拠は `O/kdebug/gfinit_op/pll2_zm_reg_decode.inc:3`、`reset_expand.inc:10`、`D/GF6200_4A_DESIGN.md:263`。nouveau 全機種対応への拡張はしない。
- 72: 下表の最終ファイルは `hwdev/gf/display/<語幹>.c`（上位サービスと PC-98 接続は例外を明記）。生成列の並びをこれらのファイルに分散複製しない。
- 元 76 行: | 情報・モード一覧 `modes` | 既知装置と 1 モード、現在モードを返す | `dispnv04/crtc.c:643`（設定側。OS32 の一覧そのものは無し） | 4F00/01/03、pp.25/30/44 | `hwdev/gfinit_mode.h:6`／定数を共有、一覧接続は service の新規分に含む |
- 元 77 行: | モード設定 `modeset` | 中層から共有列を呼ぶ組立て | `dispnv04/crtc.c:708,731`、`hw.c:827` | 4F02、p.40 | `hwdev/gf_load_state.c:11`／共有、接続は service の新規分に含む |
- 元 78 行: | timing `timing` | H/V→標準 CR・overflow、固定 progressive | `dispnv04/crtc.c:237–413` | 02 の内部 | `kdebug/gfinit_mode.c:33–77`／移動 |
- 元 79 行: | 画素形式 `format` | PACKED8・GR/AR の値（GENERAL は dac_format） | `dispnv04/crtc.c:418–449,589` | 01/02 の内部 | `kdebug/gfinit_mode.c:78–101`／移動 |
- 元 80 行: | pitch `pitch` | 640 byte、CR13/19/42 の値 | `dispnv04/crtc.c:838–908` | 4F06、p.48 | `kdebug/gfinit_mode.c:64–69`／移動、任意変更は後続 |
- 元 81 行: | 表示先頭 `start` | START=0、単位は BAR1 相対 byte | `dispnv04/crtc.c:838–908`、`hw.c:772` | 4F07、p.50 | `kdebug/gfinit_mode.c:96`・共有列／移動、非零は後続 |
- 元 82 行: | PLL `pll_program` | solver と 5 書込み、選択 50C・524 | `nvkm/subdev/devinit/nv04.c:200`、`dispnv04/hw.c:468` | 4F0B p.59／02 の内部 | `kdebug/gfinit_pll.c:196`／移動、任意 clock の公開無し |
- 元 83 行: | パレット `palette` | mask/index/RGB、部分更新、256 項の shadow | `dispnv04/hw.c:795–810`、`crtc.c:778` | 4F09、p.54 | `kdebug/gfinit_plan_c.c:518`、`gfinit_bus.c:195`／アクセスを共有、新接続 |
- 元 84 行: | DAC 精度 `dac_format` | 初版 8bit 固定、GENERAL の設定と情報 | `dispnv04/crtc.c:589–606` | 4F08、p.53 | `kdebug/gfinit_mode.c:98`／移動、6bit 切替は後続 |
- 元 85 行: | protect/blank `blank` | VGA protect と画面 blank、同期停止とは別 | `dispnv04/hw.c:110–124`、`hw.h:243`、`crtc.c:721` | Core の独立機能無し | `hwdev/generated/gf_mode_rows.inc:1` の P/L1/L3／操作部分移動 |
- 元 87 行: | GF 出力選択 `output` | head A/OR0、DACCLK・TEST、他経路を停止 | `dispnv04/dac.c:375–409`、`dfp.c:93–110` | Core の独立機能無し | `kdebug/gfinit_mode.c:97–105`・共有列／移動、TV/DVI 無し |
- 元 88 行: | cursor `cursor` | offset=0・非表示だけ | `dispnv04/cursor.c:9–59` | Core に該当無し | `kdebug/gfinit_mode.c:82–94`・共有列／移動 |
- 元 89 行: | 表示 FIFO `fifo` | NV44A burst=4、lwm=C0、CR1B/20/47 | `dispnv04/arb.c:239–264`、`hw.c:712` | 02 の内部 | `kdebug/gfinit_mode.c:92`・共有列／移動 |
- 元 90 行: | 拡張状態 `extended` | 60081C/850・CR33/52 等、overlay 停止 | `dispnv04/hw.c:665–772` | 02 の内部 | `kdebug/gfinit_mode.c:85–100`・共有列／移動 |
- 元 91 行: | 基準状態 `baseline` | init 後に一度採取、再 modeset の入力 | `dispnv04/hw.c:813`（採る範囲は OS32 限定） | 4F04 p.45 の一部分 | `kdebug/gf_stage_state.c:20`／I/O 非依存部分を共有化 |
- 元 92 行: | 停止 `quiesce` | 1850→1854→PMC140 を 0、旧 timing は戻さない | 完全対応無し、PC-98 共存上の差分 | 04 の完全復元ではない | `kdebug/recover_gf.c:125`／列を共有化 |
- 93: | 98 切替・復帰 `drivers/video/pc98/pc98_route.c` | 0FACh、decode 確認、98 再描画 | 無し、PC-98 固有 | 無し | `kdebug/recover_profile.c:42–71`、`T/TASK_GEFORCE6200.md:86`／低層だけ共有化 |
- 94: | LFB 転送 `drivers/video/geforce/gf_present.c` | RAM から BAR1 へ dirty 矩形の CPU 転送 | 該当する OS32 の転送処理は無し | LFB は 01/02、銀行窓 4F05 p.46 は不使用 | `drivers/video/pc98/backend_pegc.c` の HAL 契約を手本／新規 |
- 元 97 行: B の timing/format/pitch/fifo/extended/output/cursor/dac_format 等は既存 calc の機械的分割で各ファイルにする（F9）。値計算の再実装はしない。modeset の順番・待ち・条件は `gf_load_state` だけが所有する。
- 101: `include/video_service.h` と `hwdev/gf/service.c` に内部サービス、`hwdev/gf/internal.h` に部品間契約を置き、P0 で固定する。HAL に GF のレジスタ・ROM・recover token を出さない。
- 126: P0 の部品間契約の詳細は `hwdev/gf/internal.h`。runner が解読後・dispatch 前に REPEAT の `end_repeat` を対応する END_REPEAT PC で補完し、run は body-PC/end-PC/残り回数を frame に置く。命令内の待ちはすべて `gf_init_wait` に通し、累積待ちの一括計上・1000us 以下の分割・各刻みの前後の中断点/期限検査は runner が実装する。
- 元 127 行: 局所退避は context の有限表に書換え前に `gf_init_save` / `gf_init_save_indexed` で登録し、失敗時は adapter の `restore(opaque, ctx)` が逆順に戻す（indexed は選択情報も保持）。RESET は184Cを、CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。PLL2/IO_RESTRICT_PLL2 と P3 PLL は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。runner/dispatcher/待ち/退避ヘルパーの実体は P2 の sol、27本の run はすべて terra が並列実装する。
- 144: `gfx_core.c` は分割せず、mode_fits/bind_client/sf_backend/select 周辺を sol が約 30〜60 行修正する（F2、`O/gfx/gfx_core.c:132,285,926`）。核の起動設定・予約接続と backend 登録/ver 表示は別の小差分で扱う。
- 145: SURFACE の GF 識別定数は既存 ID の末尾へ追加し、`O/kernel/pgalloc.h:283`・`sdk/include/os32/os32_surface.h:11`・`exec/surface_query.c:66–68,110` を追従する。面数 16 枠と ABI は P0/P5 で確認する。
- 159: **時間（F11）**：devinit の同期停止は 1〜5 秒程度を仮置き（未実測の推測）、本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。ROM 採取前から基準値採取まで計時し、P6 で実測と GUI 表示までの総時間を記録する。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。
- 164: P6 までは診断 A2/main の旧展開を比較用に残す。**runner の実機受入後、診断も同じ A2/main runner＋記録付き bus に移し、schedule.inc・emit.inc・*_expand.inc と専用の旧接続を削除する**（F4）。診断の段別承認/記録/中断/S0 は adapter に残し、C plan・A1/A4/VPLL/virtual が使う汎用機能は維持する。
- 元 179 行: ## 5. 既存実装の扱いと機械的分割
- 元 181 行: AST で関数/文の範囲を得て対応表から機械切出しし、static・順序・条件コンパイル・著作権を保持する。まず同じ TU の元位置へ `.inc`、機械語一致と host の受入後に独立 `.c` 化する（`O/docs/POLICY_DEV.md:132–138`）。
- 元 182 行: 第一段は同じ flags の objdump で命令と relocation の symbol/addend を比較する。独立 `.c` 化の最適化差は別差分で説明し、移動を新規実装に数えない。本起草ではビルド/試験はしない。
- 元 184 行: | 現行と抽出範囲 | 行き先・今回の扱い |
- 元 185 行: |---|---|
- 186: | `kdebug/gfinit_decode.c:12–65`・`gfinit_op/*_decode.inc`・bytes/tables の解読部分 | switch と既存 include 構成を保って共有。recover 型からの切離しは薄い接続だけ。27 本へ再分配しない。expand_step 等の旧展開は P6 までだけ存続。 |
- 187: | `gfinit_exec.c` 125 行（wait:16、control:27、run:80） | 必要な wait/control の範囲を同 TU で機械切出しし、既存意味を runner の手本にする。旧 plan executor の丸ごと移植や恒久的な二重実装はしない。 |
- 188: | `gfinit_pll.c` 246 行、helpers 56–77、parse 78–124、solve 125–191、sequence 192–246 | `gfinit_pll/{limits,parse,solve,sequence}.inc`→`hwdev/gf/pll/`。共通 solver は一つ。 |
- 元 189 行: | `gfinit_mode.c:33–111` calc（141 行のファイル内）：timing/pitch | timing は 42–77 の timing 文と 96 の config、pitch は 64/68/69 の pitch 文→各 `display/{timing,pitch}.c`。共有行は文境界で機械切出しする。 |
- 元 190 行: | 同 calc：format/extended | format=53/79–81/91、extended=85–90/93 の engine_ctrl/94–95/99–100→各 `display/{format,extended}.c`。各代入は一度だけ移す。 |
- 元 191 行: | 同 calc：cursor/fifo/start | cursor=92–93/98 の cursor 関連文、fifo=92 の burst/lwm、start=96 の start→各 `display/{cursor,fifo,start}.c`。 |
- 元 192 行: | 同 calc：DAC/output/PLL | dac_format=98 の general、output=101、PLL=104–110→各 `display/{dac_format,output,pll_program}.c`。ゼロ初期化・順序・依存を保存し、新規計算は無し（F9）。 |
- 元 193 行: | 同 calc の下処理と残り | 最初は対応する文区間を元位置の `.inc` として一度ずつ残す。独立 `.c` 化時に各機能を一つにまとめ、mode_bit と zero 初期化は共有、集約関数は呼出しだけ。palette:112–123 と模様 row:124–141 は診断に残す。 |
- 194: | `gfinit_resources.c` 211 行、36–107/109–144/146–164/166–211 | bounds/bars/windows/scan の機械分割→`hwdev/gf/resources/`。読取り検査を共有し、F6 の ID/定数長接続を加える。A1 sizing は診断専用。 |
- 195: | `hwdev/gf_load_state.c` 33 行・`generated/gf_mode_rows.inc` | 分割/複製無し。唯一の生成元 `tools/gfinit_mode_tables.py:29` を維持。blank/DPMS の modeset 順序もここに保持。 |
- 元 196 行: | `gfinit_plan_c.c` 529 行、baseline:159–186・checks:187–275 | 本番で共有する baseline/CHECK の区間だけ同 TU の `.inc` へ機械切出しし、共有値・descriptor を独立化する。診断の node 生成接続、446 の呼出し、rows/build/control/vram/color は元の場所に残す（N3）。 |
- 197: | `gfinit.c` 541 行、ROM:143–212 | 本番で共有する ROM の区間だけ機械切出しし、共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。P6 の A2/main 呼出し変更は維持する（N3）。 |
- 元 198 行: | `gf_stage_state.c` 73 行・`gfinit_bus.c` 211 行 | baseline 値採取と局所復元だけ必要範囲を共有化。診断 recover 型の adapter を残す。 |
- 199: | `recover_gf.c:125–129`・`recover_profile.c:42–71` | 本番が共有する戻し部品は quiesce と 98 セル手順の小さな二つ。記録/画面復帰 adapter は各層に置く。**recover.c の分割は GF_STAGES T3 に任せ、本票の作業・見積りに含めない**（F1、`D/GF_STAGES_DESIGN.md:162`）。 |
- 200: | `gfx/gfx_core.c` 1,251 行・既存 PEGC/Cirrus | gfx_core は §4 の局所変更だけ、分割無し。`backend_pegc.c` 1,215 行・`backend_cirrus.c` 606 行は参考として読む。 |
- 元 202 行: 移した実体の旧コピーは消す。診断 A1/A2/main/A4/C-only、CLI、記録、C-only の常駐 ROM/plan/lease は必要な寿命を保つ。P6 の展開削除時にも他診断が使う実体を巻き込まない。
- 元 203 行: build/生成契約/source を読む試験/check_map/p2v 許可表/変異目印を追従する。GNU11・kstring・定数三層管理・物理変換（[C1–5]）と既存 KAPI 配置/cdecl（[ABI1–3]）を維持する。公開 ABI 更新が必要なら kapi.json・末尾追加・全再構築を P5 に含める。
- 元 205 行: ## 6. 段の分解・並列割当て・受入
- 元 207 行: 実装前に PM が機能表・契約・割当て・試験/実機受入・見積りをユーザーへ説明し了承を得る（`T/TASK_GF_DRIVER.md:32`）。この設計票作成は実装承認ではない。以下は将来の作業。
- 元 208 行: 新規ファイルは約 500 行が目標で強制ではない。1,000 行超を触る担当は sol/astra、それ以外は terra 可。共有ヘッダ/dispatcher/生成器の担当は一人に固定する。
- 元 210 行: | 段 | 内容・触るファイルと規模見込み | 担当・並列条件 | 試験・受入 |
- 元 211 行: |---|---|---|---|
- 212: | P0 契約 | video_service.h/internal.h 計 100〜160 行、部品/文区間の対応表 | astra/sol、直列 | Trident fake で契約成立、ABI と選択門・失敗出口を確認 |
- 元 213 行: | P1 機械分割 | §5 の小ファイル `.inc`、元 `.c` と入力登録。後段含め一意の移動計 800〜1,350 行 | terra 可、TU ごと並列。大規模分割無し | objdump 一致、既存対象 host、GF 無し NP21/W の 98 退行 |
- 214: | P2 A/runner | op 27 本各 5〜30 行・計 300〜500、runner 120〜200、共有 decode/PLL/resources 接続 | terra で命令分担、dispatcher は sol。P0/P1 後 | 実 ROM fixture（27 種/193 命令）の旧展開との I/O trace 差分試験 1 本＋制御個別 case |
- 元 215 行: | P3 B | calc の機械分割各 5〜40 行程度、row adapter 100〜160、palette/blank/stop 各 20〜70、service 80〜140 | terra、P2 と並列可。共有列/C 接続は sol | 移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK |
- 216: | P4 起動/復帰 | devinit 100〜180、bus/基準値/ROM は移動主体、選択 export/核接続は数十行 | sol/astra 統合、部品は terra。P2/P3 後 | KDEBUG=0 fake 起動、初回 Command 書込みは 1 回・BAR 書込みは 0、0x0106→0x0102 の 16bit 確認と不一致時の停止、A2/main順・期限・各失敗出口、選択中 gfinit 拒否、再 devinit 無し |
- 217: | P5 HAL/配備 | backend 180〜260、present 60〜100、route 40〜70、module/設定/台帳接続 80〜130、gfx_core 局所 30〜60 行 | sol が接続、HAL/present は terra。P4 と fake 経由で並列可 | GUI=1拒否・GUI≠1許可（省略時0）、GUI 起動・CUI 描画アプリ起動の各場合で、起動 1 回につき最初の gfx_init で 1 回だけ devinit、dirty/pitch/palette、CLIENT は PEGC/GF 一方だけ、pc98 fallback、decode不明時98無書込み、shutdown→再init、部品欠落/KDEBUG=0。NP21/W は GF 無し拒否と98/PEGC/Cirrus退行 |
- 218: | P6 本番受入→診断統一→削除 | 下記実機 1 回。その後 A2/main の記録 bus adapter（80〜140 行目標）へ接続、旧展開と不要 glue を削除 | PM＋ユーザー立会い、整理は sol/terra。runner の実機合格が診断移行の前提 | 実機 5 場面と機械項目→保存した差分 trace/制御 case/診断段別承認・記録・中断/S0/C-only退行で統一を確認。旧展開削除までを完了条件にする |
- 221: 既存試験は機能別の入口へ移し、fake bus/clock を共有する。P6 削除前に旧経路の trace を fixture 化し、手本由来の独立期待値と併用する。削除後も旧 executor を試験用コピーとして残さず、製品表から期待値を作って自己一致を主張しない。
- 元 222 行: 各段は必要 build/対象 host、最終統合は PM の既存手順で一回。試験/fixture/台本/build 接続の新規・書換えは合計 600〜1,000 行目標。NP21/W を GF 映像の証拠にしない。
- 元 223 行: **立会いは本番だけ 1 回、目視 5 場面**（U7 決定）。診断 C は T2 で受入済みという PM 判断を引き継ぎ、解読と共有列を保持するため再受入の回を設けない。失敗修正後の追加立会いは必要になり得る。
- 元 224 行: 当日は `GUI=0,GFX=geforce` で 98 CUI 起動→手動 GUI（GF①）→通常終了して 98 文字/入力更新（98②）→再 GUI、C 相当だけ（GF③）→既存中止経路または受入用打切りで 98 復帰（98④）→reboot 後の 98 CUI（98⑤）。危険なレジスタ値は注入しない。
- 元 225 行: 機械項目は版/ROM/ID/BAR/Command/legacy、初回 Command 書込み 1 回・BAR 書込み 0、devinit 回数=1・再 GUI で A2/main 増加無し、CR52 非累積、必須 CHECK、palette/dirty の処理結果、route/セル、終端、devinit 実測秒数・初回 GUI 総時間。目視は同期・色・文字/マウス更新を判定し、レジスタ一致で代用しない。
- 226: 無応答/同期外れ/98 復帰失敗なら中止し、既存 S0→reboot/物理復帰へ進む。ログ欠落・判定器エラーで戻しを止めず、現場で新しい修復列/失敗分類を追加しない。P6 の診断統一は host で確認した範囲を記録し、移行後診断の新たな実機合格を称さない。
- 元 228 行: ## 7. 行数・容量の見込み
- 元 230 行: 移動 M は元の一意な行を一度だけ数え、純増 0。新規 N は意味を書き換える行も含み、削除 D は移動元を除く置換旧行/廃止実体。以下は推測で、詰め書きや必要処理の削除を求めない。
- 元 232 行: | 区分 | 新規/書換え N | 移動 M | 置換/廃止 D |
- 元 233 行: |---|---:|---:|---:|
- 元 234 行: | A：27 run・runner/devinit・共有解読/PLL/resources 接続・P6 診断 adapter | 約 600〜1,000 | 500〜850 | 400〜650 |
- 元 235 行: | B：service・row adapter・palette・blank・stop（calc/CHECK/基準値は移動） | 300〜500 | 200〜300 | 40〜80 |
- 元 236 行: | OS32：HAL/present/route・予約/module/設定/台帳・共通ヘッダ | 500〜850 | 100〜200 | 20〜70 |
- 元 237 行: | **製品合計** | **約 1,400〜2,350** | **800〜1,350** | **460〜800** |
- 元 238 行: | host/fixture/受入資料/build・生成器接続（別枠） | **600〜1,000** | 既存 case の必要範囲のみ、実装時計数 | 移動元以外の旧比較/glue を実装時計数 |
- 元 240 行: N2：A の内訳は run 計 300〜500＋runner 120〜200＋devinit 100〜180＋P6 adapter 80〜140＝600〜1,020 行で、接続を各項に含め、概算を約 600〜1,000 行とする。製品 N は B/OS32 を加え約 1,400〜2,350 行。
- 元 241 行: N3：M は本番共有区間で再見積り。A は ROM 143–212 を含む解読/PLL/資源等で 500〜850、B は calc 33–111・baseline 159–186・checks 187–275 の計 196 行を主に、必要な値採取等を含め 200〜300、OS32 は従前の 100〜200、計 800〜1,350 行。診断専用の rows/build/control/vram/color・lifetime/stages は M に含めない。
- 元 242 行: recover.c/gfx_core.c の大規模分割は計上しない。B の calc 分割を新規に数えず、A の削除には P6 の schedule/emit/*_expand と専用旧接続を含む。必要な解読/共有計算/他診断の実体は削除数に含めない。最終 diff で N/D/M を再計数する。
- 244: 常駐は palette 768 B＋mode/基準値/資源など数 KiB の見込み。診断の常駐 ROM/plan/lease は別の起動で既存寿命を維持する。image/mem・loader span は未計測で、P4/P5 に実測し上限超過は PM に報告する。
- 元 245 行: 27 run だけ新設、解読/整数 solver/生成列/計算の共有、1 mode・加速無し・CLI 新設無しで増加を抑える。Cirrus 全体約 939 行を規模の比較材料とする（`opus_r1.md:14`）。行数目標を強制上限にしない。
- 元 247 行: ## 8. ユーザーに決めてもらう所
- 元 249 行: **無し。** PM は U1=640×480×256、U2=GUI≠1（省略時0）で GFX=geforce の明示選択、U3=ID 限定の定数長/BAR 無書込み、U4=palette/blank/start=0/cursor 非表示、U5=geforce.mod、U6=専用 runner＋受入後の診断統一/展開削除、U7=本番 1 回・5 場面を決定済み。
- 元 250 行: 本票はその決定を反映した実装説明の材料であり、実装着手前の PM の詳細説明・了承は別途必要。本作業は設計票と変更一覧の作成だけで、コード変更・ビルド・試験・NP21/W・実機操作は未実施。

> [要確認] CR3C の組の照合は、現行実装では COPY の直後に読み返していない。
> [要確認] `gf_instruction` は現行実装では `gfinit_instruction`（`hwdev/gf/internal.h:115`）。
