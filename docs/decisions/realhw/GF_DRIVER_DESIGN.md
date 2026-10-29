# GF_DRIVER_DESIGN — GeForce 6200 の本番の画面ドライバ — 確定設計

> 状態: **現行 (2026-10-29 確定)** — astra 起草 (r1〜r3)、Opus のレビュー 3 往復 (r1 要修正・r2 要修正・r3 確定可、R1 の文言は PM が直した)。往復の記録は `~/os32-tmp/run/gf-driver/`。票は TASK_GF_DRIVER (docs/tasks/realhw/)。**実装の前に PM がユーザーに詳細な実装計画を説明し、了承を得る。**
> 起草主体: Codex（GPT-6 系）。詳細モデル ID は環境から確認できず未確認。依頼の指定は `gpt-6-astra`。

根拠の `O/` は `/home/hight/os32-v3/`（参照基点 `39a3461a`）、`D/` は `O/docs/decisions/realhw/`、`T/` は `O/docs/tasks/realhw/`。
`R/` は `/home/hight/os32-tmp/refs/geforce6200/`、`N/` は `R/nouveau/drivers/gpu/drm/nouveau/`、`I:行` は `N/nvkm/subdev/bios/init.c:行`。
nouveau は c03c1a1 の MIT 部を使う（`R/INDEX.md:9`、`N/dispnv04/hw.c:6`）。ライセンスと著作権表示を移動先にも保持する。
VBE は [VESA VBE Core Functions 3.0（1998-09-16、公開 PDF）](https://www.read.seas.harvard.edu/~kohler/class/04f-aos/ref/hardware/vbe3.pdf) の機能一覧だけを参照する。以下の VBE 頁は本文の頁番号。
NV44A のメーカー製プログラミングマニュアルは今回の参照ディレクトリでは確認できない。既に採用済みの nouveau・確定設計・実測を根拠とし、新しい未知レジスタ操作を補わない（`O/docs/CONSTRAINTS.md:94–101`）。
**決定**は `T/TASK_GF_DRIVER.md:7–32`・確定設計・`BRIEF_r2.md:2` の PM 決定（`opus_r1.md:2–15` の全指摘採用）から引き継ぎ、`BRIEF_r3.md:2` の N1〜N5 全採用を反映する。以下の新しい関数名・配分・所要時間・行数は実装案／見積り（推測）。

## 1. 目的と範囲

**決定**：下層＝初期化命令と表示操作の両方を 1 機能 1 ファイル、中層＝VBE に近い少数のサービス、上層＝OS32 HAL の三層にする。
**決定**：実装済み部分を機械的に分割してから変更する。中層と部品間の契約を先に固め、独立した小ファイルを terra 中心に並列実装する。
**決定**：本番 modeset は nouveau の load_state に近い簡素な作りとし、試験 C と `gf_load_state`・生成列を同一ソースで使う（`D/GF_STAGES_DESIGN.md:59–63`）。
OS32 は単一利用者・単一タスク・協調切替。頑丈さの対象はハードの誤動作とバグであり、有限待ち・アクセス範囲・後始末・98 への復帰を守る。権限分離や攻撃者向けの検査は追加しない。
**初版の決定範囲**は Ra266 の 0:13.0、10de:0221、subsys 10fc:1270 の NV44A、既知 ROM の 27 種・193 命令、head A・ANALOG OR0・RGB-IN。
表示は **640×480、PACKED8、256 色、pitch=640、表示先頭=0、25.175 MHz 要求**の 1 モードとする。既存 C の定数・計算を移す（`O/hwdev/gfinit_mode.h:6–20`、U1 決定）。
実 ROM の解は N1/M1=26/7、N2=M2=1、P=2、actual=25,071 kHz。要求値との差 104 kHz、約 0.41%。36 MHz 診断値を使わない（`D/GF6200_4C_DESIGN.md:181`）。
GUI とグラフィック内コンソールは CPU 描画→HAL 転送。98 のハードウェア文字合成は GF に無い。通常のテキスト CUI は 98 側に残す。
2D/3D 加速、TV/DVI、head B 表示、任意 timing、高解像度・true color、EDID 自動モード選択、ページフリップ、VGA BIOS の x86 実行・解析は作らない。
カーソルは初版では GF ハードカーソルを非表示にするだけで、GUI のソフトウェア描画を使う。FIFO は表示用の設定を扱い、描画エンジンのコマンド FIFO は作らない。
初版は独立 DPMS・非零の表示先頭・HW cursor を提供せず、DPMS は modeset 内の OFF/ON だけ（U4 決定）。`GFX=geforce` は `GUI≠1` のとき許し、auto の候補にはしない（U2 決定）。

## 2. 機能の一覧

### 2-A. 初期化スクリプト 27 命令

**解読は現行の switch と `.inc` をそのまま共有する**。実 ROM で確認済みの解読を 27 本へ配り直さない（F3、`O/kdebug/gfinit_decode.c:18–55`）。
下表の短い `.inc` は `O/kdebug/gfinit_op/` 以下、`decode.c` は `O/kdebug/gfinit_decode.c`。D/E は decode/expand。省略した E が本体に残る行は本体位置を併記した。
最終の部品は `hwdev/gf/op/<命令名の小文字>.c` の **run だけ 27 本、各 5〜30 行、計 300〜500 行目標**。共通計算は共有する。下表 E は移行元で、P6 で診断 A2/main も同じ runner に移し、静的展開を削除する（F4）。

| opcode・命令名（最終ファイルの語幹） | 今の所在（D / E） | nouveau の対応 |
|---|---|---|
| 32 IO_RESTRICT_PROG | `io_restrict_decode.inc:1` / `io_restrict_expand.inc:1` | I:621 `init_io_restrict_prog` |
| 33 REPEAT | `repeat_decode.inc:1` / `decode.c:89`、`gfinit_decode/schedule.inc:36` | I:658 `init_repeat` |
| 36 END_REPEAT | `decode.c:24` / `decode.c:90` | I:724 `init_end_repeat` |
| 37 COPY | `copy_decode.inc:1` / `copy_expand.inc:1` | I:740 `init_copy` |
| 38 NOT | `decode.c:24` / `decode.c:91` | I:767 `init_not` |
| 39 IO_FLAG_CONDITION | `condition_decode.inc:1` / `condition_expand.inc:1` | I:779 `init_io_flag_condition` |
| 4A IO_RESTRICT_PLL2 | `io_restrict_decode.inc:1` / `io_restrict_expand.inc:1`、`pll_expand.inc:1` | I:950 `init_io_restrict_pll2` |
| 4B PLL2 | `pll2_zm_reg_decode.inc:1` / `decode.c:99`、`pll_expand.inc:1` | I:985 `init_pll2` |
| 51 CR_INDEX_ADDRESS_LATCHED | `cr_index_address_latched_decode.inc:1` / 同 `_expand.inc:1` | I:1146 `init_cr_idx_adr_latch` |
| 52 CR | `cr_decode.inc:1` / `cr_index_io_expand.inc:1` | I:1176 `init_cr` |
| 53 ZM_CR | `cr_decode.inc:1` / `cr_index_io_expand.inc:1` | I:1196 `init_zm_cr` |
| 54 ZM_CR_GROUP | `decode.c:21` / `zm_cr_group_expand.inc:1` | I:1213 `init_zm_cr_group` |
| 58 ZM_REG_SEQUENCE | `zm_reg_sequence_decode.inc:1` / 同 `_expand.inc:1` | I:1281 `init_zm_reg_sequence` |
| 5B SUB_DIRECT | `decode.c:48` / `decode.c:95`、`gfinit_decode/schedule.inc:28` | I:1344 `init_sub_direct` |
| 5F COPY_NV_REG | `copy_decode.inc:1` / `copy_expand.inc:1` | I:1415 `init_copy_nv_reg` |
| 62 ZM_INDEX_IO | `index_io_decode.inc:1` / `cr_index_io_expand.inc:1` | I:1441 `init_zm_index_io` |
| 63 COMPUTE_MEM | `decode.c:24` / `decode.c:93` | I:1459 `init_compute_mem`、NV44A では下記の no-op |
| 65 RESET | `reset_nv_reg_decode.inc:1` / `reset_expand.inc:1` | I:1477 `init_reset` |
| 6B SUB | `sub_decode.inc:1` / `decode.c:96`、`gfinit_decode/schedule.inc:28` | I:1663 `init_sub` |
| 6E NV_REG | `reset_nv_reg_decode.inc:1` / `decode.c:100` | I:1709 `init_nv_reg` |
| 6F MACRO | `condition_decode.inc:1` / `decode.c:101` | I:1727 `init_macro` |
| 71 DONE | `decode.c:24` / `decode.c:93` | I:610 `init_done` |
| 72 RESUME | `decode.c:24` / `decode.c:92` | I:1751 `init_resume` |
| 74 TIME | `decode.c:53` / `decode.c:94` | I:1781 `init_time` |
| 75 CONDITION | `condition_decode.inc:1` / `condition_expand.inc:1` | I:1802 `init_condition` |
| 78 INDEX_IO | `index_io_decode.inc:1` / `cr_index_io_expand.inc:1` | I:1853 `init_index_io` |
| 7A ZM_REG | `pll2_zm_reg_decode.inc:1` / `decode.c:101` | I:1892 `init_zm_reg` |

COMPUTE_MEM の no-op は省略ではなく NV44A の仕様分岐。`N/nvkm/engine/device/base.c:677`→`nvkm/subdev/devinit/nv1a.c:29` に meminit が無い（`D/GF6200_4A_DESIGN.md:49`）。
head/skip、SUB の復帰、REPEAT、RESET の強制実行・184C 復元、CR のラッチ復元は各命令試験に残す。skip 中の CR44 による head 更新も維持する（`O/kdebug/gfinit_decode.c:79–96`）。
OS32 固有差分も保持：PMC_ENABLE bit0 を立てる、RESET 後 1850 は 0、CR3C の組を確認する。根拠は `O/kdebug/gfinit_op/pll2_zm_reg_decode.inc:3`、`reset_expand.inc:10`、`D/GF6200_4A_DESIGN.md:263`。nouveau 全機種対応への拡張はしない。

### 2-B. 表示サービスに当たる操作

VBE の対応は機能の比較であり INT 10h 互換 ABI を提供する意味ではない。VBE に独立した機能が無い内部操作は「02 の内部」または「無し」と明記する。
下表の最終ファイルは `hwdev/gf/display/<語幹>.c`（上位サービスと PC-98 接続は例外を明記）。生成列の並びをこれらのファイルに分散複製しない。

| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 | 現行 `O/` の所在／扱い |
|---|---|---|---|---|
| 情報・モード一覧 `modes` | 既知装置と 1 モード、現在モードを返す | `dispnv04/crtc.c:643`（設定側。OS32 の一覧そのものは無し） | 4F00/01/03、pp.25/30/44 | `hwdev/gfinit_mode.h:6`／定数を共有、一覧接続は service の新規分に含む |
| モード設定 `modeset` | 中層から共有列を呼ぶ組立て | `dispnv04/crtc.c:708,731`、`hw.c:827` | 4F02、p.40 | `hwdev/gf_load_state.c:11`／共有、接続は service の新規分に含む |
| timing `timing` | H/V→標準 CR・overflow、固定 progressive | `dispnv04/crtc.c:237–413` | 02 の内部 | `kdebug/gfinit_mode.c:33–77`／移動 |
| 画素形式 `format` | PACKED8・GR/AR の値（GENERAL は dac_format） | `dispnv04/crtc.c:418–449,589` | 01/02 の内部 | `kdebug/gfinit_mode.c:78–101`／移動 |
| pitch `pitch` | 640 byte、CR13/19/42 の値 | `dispnv04/crtc.c:838–908` | 4F06、p.48 | `kdebug/gfinit_mode.c:64–69`／移動、任意変更は後続 |
| 表示先頭 `start` | START=0、単位は BAR1 相対 byte | `dispnv04/crtc.c:838–908`、`hw.c:772` | 4F07、p.50 | `kdebug/gfinit_mode.c:96`・共有列／移動、非零は後続 |
| PLL `pll_program` | solver と 5 書込み、選択 50C・524 | `nvkm/subdev/devinit/nv04.c:200`、`dispnv04/hw.c:468` | 4F0B p.59／02 の内部 | `kdebug/gfinit_pll.c:196`／移動、任意 clock の公開無し |
| パレット `palette` | mask/index/RGB、部分更新、256 項の shadow | `dispnv04/hw.c:795–810`、`crtc.c:778` | 4F09、p.54 | `kdebug/gfinit_plan_c.c:518`、`gfinit_bus.c:195`／アクセスを共有、新接続 |
| DAC 精度 `dac_format` | 初版 8bit 固定、GENERAL の設定と情報 | `dispnv04/crtc.c:589–606` | 4F08、p.53 | `kdebug/gfinit_mode.c:98`／移動、6bit 切替は後続 |
| protect/blank `blank` | VGA protect と画面 blank、同期停止とは別 | `dispnv04/hw.c:110–124`、`hw.h:243`、`crtc.c:721` | Core の独立機能無し | `hwdev/generated/gf_mode_rows.inc:1` の P/L1/L3／操作部分移動 |
| DPMS `dpms` | SR01→10ms→CR17→CR1A、modeset の OFF/ON は必須 | `dispnv04/crtc.c:175–234`、`dac.c:460` | 拡張 4F10、p.63 | 同共有列 P/commit／共有、独立公開無し（U4） |
| GF 出力選択 `output` | head A/OR0、DACCLK・TEST、他経路を停止 | `dispnv04/dac.c:375–409`、`dfp.c:93–110` | Core の独立機能無し | `kdebug/gfinit_mode.c:97–105`・共有列／移動、TV/DVI 無し |
| cursor `cursor` | offset=0・非表示だけ | `dispnv04/cursor.c:9–59` | Core に該当無し | `kdebug/gfinit_mode.c:82–94`・共有列／移動 |
| 表示 FIFO `fifo` | NV44A burst=4、lwm=C0、CR1B/20/47 | `dispnv04/arb.c:239–264`、`hw.c:712` | 02 の内部 | `kdebug/gfinit_mode.c:92`・共有列／移動 |
| 拡張状態 `extended` | 60081C/850・CR33/52 等、overlay 停止 | `dispnv04/hw.c:665–772` | 02 の内部 | `kdebug/gfinit_mode.c:85–100`・共有列／移動 |
| 基準状態 `baseline` | init 後に一度採取、再 modeset の入力 | `dispnv04/hw.c:813`（採る範囲は OS32 限定） | 4F04 p.45 の一部分 | `kdebug/gf_stage_state.c:20`／I/O 非依存部分を共有化 |
| 停止 `quiesce` | 1850→1854→PMC140 を 0、旧 timing は戻さない | 完全対応無し、PC-98 共存上の差分 | 04 の完全復元ではない | `kdebug/recover_gf.c:125`／列を共有化 |
| 98 切替・復帰 `gfx/pc98_route.c` | 0FACh、decode 確認、98 再描画 | 無し、PC-98 固有 | 無し | `kdebug/recover_profile.c:42–71`、`T/TASK_GEFORCE6200.md:86`／低層だけ共有化 |
| LFB 転送 `gfx/gf_present.c` | RAM から BAR1 へ dirty 矩形の CPU 転送 | 該当する OS32 の転送処理は無し | LFB は 01/02、銀行窓 4F05 p.46 は不使用 | `gfx/backend_pegc.c` の HAL 契約を手本／新規 |

VBE 4F0A（保護モード入口、p.56）は実装しない。blank、FIFO、カーソル、PC-98 の出力リレーを VBE Core のサービス数に数えて水増ししない。
B の timing/format/pitch/fifo/extended/output/cursor/dac_format 等は既存 calc の機械的分割で各ファイルにする（F9）。値計算の再実装はしない。modeset の順番・待ち・条件は `gf_load_state` だけが所有する。

## 3. 抽象化の層の取り決め

`include/video_service.h` と `hwdev/gf/service.c` に内部サービス、`hwdev/gf/internal.h` に部品間契約を置き、P0 で固定する。HAL に GF のレジスタ・ROM・recover token を出さない。
```c
struct video_device; /* 私有 context と関数表、装置ごとに一つ */
struct video_mode { u32 id, width, height, pitch, format, dac_bits, clock_khz; };
struct video_info { u32 caps, mode_count, current_mode, active; };
struct video_fb { u32 phys, bytes, pitch, format; };
int video_query(struct video_device *d, struct video_info *out);
int video_mode_info(struct video_device *d, u32 index, struct video_mode *out);
int video_start(struct video_device *d, u32 mode_id, const u8 *rgb256);
int video_framebuffer(struct video_device *d, struct video_fb *out);
int video_palette(struct video_device *d, u32 first, u32 count, const u8 *rgb);
int video_display_start(struct video_device *d, u32 byte_offset);
int video_blank(struct video_device *d, int blank);
int video_stop(struct video_device *d);
```
成功 0、引数・範囲不正は既存 `OS32_ERR_INVAL`、未対応は `OS32_ERR_NOSYS`、装置処理失敗は既存下層と同じ -1。新しい失敗分類を作らず、最後の段・対象・既存 rc を保持する。
`video_start` は初回だけ devinit→modeset。stop は局所後始末→quiesce、続けて上層が 98 切替を行う。停止済み stop・同値 blank は成功。start-offset は BAR1 相対 byte、0 だけ成功、非零は NOSYS。
mode ID と配列 index は区別する。format は既存 PACKED8、RGB は各 8bit。入力は呼出し中だけ借用し必要値を shadow へコピーする。情報取得は I/O・割当て・再初期化をせず、失敗時 out をゼロにする。
FB の bytes は表示用予約 307,200 B で BAR1 全長とは別。palette の first/count と FB の offset/pitch/height を既存範囲内で検査し、不正なら I/O 前に返す。active/能力には成功済みの機能だけを載せる。
状態は BAR raw/base/length・写像・ROM 識別・PLL limits・初期化済み・基準値 valid・mode/palette/blank・stop 成否・再利用可否。同期・非再入で扱う。
基準値は main4 後に CR1C/33/3C/4B/52・60081C/600850/680524、初回 C040、PMC_ENABLE を一度だけ採る。全部成功した時だけ valid、CR52=(初回+4)&FF を維持する（`D/GF_STAGES_DESIGN.md:67–74`）。
局所 index/lock/PLL 途中の退避値は基準値と分ける。初期化失敗、bus/窓/stop 失敗、ID/BAR 変化は再利用不可。再 devinit はせず、正常 stop 後の ID/BAR/Command/legacy/PMC 照合成功時だけ C 相当の再 modeset を許す（同:78–92）。
Trident は自身の mode/FB/DAC 情報を返し、未対応 start-offset/blank は NOSYS。GF=01 と Trident の 0FACh 選択値はボード側へ分ける（`T/TASK_TRIDENT_DRIVER.md:426–444`）。独立 DPMS と VBE 状態 blob の完全復元は初版に含めない。
下層 A の契約は `int gf_op_<name>_run(struct gf_init_context *, const struct gf_instruction *)`。解読は現行 `decode_instruction` と `.inc` の一つの実体を共有し、命令別 decode API は増やさない。
context は head/skip、有限の呼出し/反復 stack、ROM 借用参照、既存上限・時計・`gf_io`。bus は read/write（BAR/offset/width/値）、wait_us、point、局所復元。診断の記録・承認は adapter が担当する。
P0 の部品間契約の詳細は `hwdev/gf/internal.h`。runner が解読後・dispatch 前に REPEAT の `end_repeat` を対応する END_REPEAT PC で補完し、run は body-PC/end-PC/残り回数を frame に置く。命令内の待ちはすべて `gf_init_wait` に通し、累積待ちの一括計上・1000us 以下の分割・各刻みの前後の中断点/期限検査は runner が実装する。
局所退避は context の有限表に書換え前に `gf_init_save` / `gf_init_save_indexed` で登録し、失敗時は adapter の `restore(opaque, ctx)` が逆順に戻す（indexed は選択情報も保持）。RESET は184Cを、CR_INDEX_ADDRESS_LATCHED はラッチと元のindexを明示的に復元し、命令完了時に `gf_init_commit` で解除する。PLL2/IO_RESTRICT_PLL2 と P3 PLL は列全体の成功時だけ解除し、正常 stop で完了済みPLLを戻さない。runner/dispatcher/待ち/退避ヘルパーの実体は P2 の sol、27本の run はすべて terra が並列実装する。
B は `int gf_load_row(struct gf_display_context *, const struct gfinit_mode *, u32 ordinal)` を `gf_load_io.row` へ接続し、section/wait も既存型を使う（`O/hwdev/gf_load_state.h:36–41`）。
read/write の別名、AR の FF reset→index→data、DAC の write-only auto-increment、LIVE RMW の実行時読取り、既存の幅/範囲/有限待ちを保持する。新しい汎用 IR・計画 VM は作らない。

## 4. OS32 の層・起動・復帰

### 4-1. HAL・予約・写像

GUI/グラフィックコンソール→RAM CLIENT→`gfx_present/dirty`→`backend_geforce`→video service→GF 部品。KAPI とアプリ側描画経路を変えない（[HW1]、`O/include/gfx_hal.h:37–97`）。
`GfxBackend` の probe/prepare/init/query/present_rect/set_palette/enter/leave/shutdown/modes を埋める。fill_rect/blit は NULL、TEXT_OVERLAY・vsync・page-flip 能力は無し、CPU 転送の PACKED8 だけ。
RAM CLIENT 300 KiB と BAR1+[0,4B000h) の DISPLAY を SURFACE 台帳へ別登録し、`bb_base/bb_size/bb_pitch` と framebuffer API は CLIENT を返す。**PEGC と GF の CLIENT は一方だけ予約**し、600 KiB の二重確保を避ける（F10）。
**ID 10de:0221・subsys 10fc:1270、Ra266 の 0:13.0 に限り** BAR0=16 MiB、BAR1=256 MiB、BAR2=16 MiB の定数長を使う（F6、`O/tools/accept/gf_a4.md:25`）。長さは既知ボードの測定値であり任意の同系列製品へ一般化しない。
起動予約は ID と BAR raw/base を読むだけ。属性・base の非零/境界合わせ・加算の範囲・RAM/他装置との重なり・橋窓への収まりを既存資源検査で確認する。**本番では BAR に書かず、A1 の sizing・保存/復元を一切実行しない**。
BAR 全 decode 範囲を台帳予約し、BAR0 の必要頁と BAR1 の表示面を既存 master PD の supervisor+PCD で写像する。予約凍結後に拡張せず、実使用前は ID/base/属性の一致を読取りで確認する。
不一致・未知 ID・範囲不成立では GF を使わず pc98 を維持する。BAR1 の窓長と実 VRAM 容量は別で、既存 PFB 容量確認・表示面範囲も維持する。変換は `P2V_IO` と既存写像 API（[C5]、`O/gfx/gfx_core.c:305–327`）。
present は矩形・pitch に従う CPU 転送、行間の中断点、read-safe な GPU 読取りによる posted write 排出を保つ（`D/GF6200_4C_DESIGN.md:161`）。毎フレームの VRAM 読戻しはせず、present_bytes/commits を更新し hw_ops=0。
パレットは既存固定色/lease 方針を通った OS32 の 256 色表を使い、診断 C の 8 色表を本番へ持ち込まない（`O/gfx/gfx.h:94–98`）。モード一覧は 1 行、`gfx_init_200` の packed=480 選択を保つ。
`gfx_core.c` は分割せず、mode_fits/bind_client/sf_backend/select 周辺を sol が約 30〜60 行修正する（F2、`O/gfx/gfx_core.c:132,285,926`）。核の起動設定・予約接続と backend 登録/ver 表示は別の小差分で扱う。
SURFACE の GF 識別定数は既存 ID の末尾へ追加し、`O/kernel/pgalloc.h:283`・`sdk/include/os32/os32_surface.h:11`・`exec/surface_query.c:66–68,110` を追従する。面数 16 枠と ABI は P0/P5 で確認する。

### 4-2. 起動・devinit・診断との切替

**`GFX=geforce` は `GUI≠1` のとき許す**（F5・N4）。起動予約前に既存の設定読取りで判定し、GUI=1 なら拒否して pc98 を選ぶ。GUI 省略時は既定値 0（`O/kernel/kernel.c:521`）。最初の `gfx_init`（GUI または CUI の描画アプリ）で、承認待ち無しに devinit を一度行う（N5、`O/gfx/gfx_core.c:960,1014`）。
これで再起動時は GF devinit に入らず 98 CUI へ戻れる。起動時上書きキー等の新しい脱出機構や auto 優先候補化は足さない（`O/kernel/kernel.c:343–366,577–582`）。
prepare は識別/資源確認だけ。init で `video_start`→初回転送→98 セル確認→route01 確認まで行い、初回 enter は無操作。void init 後の probe=0 で既存失敗出口へ渡す（`O/gfx/gfx_core.c:932–945`）。
**GF 初期化失敗時は pc98 へ落とす**（同:937）。ただし legacy decode 解放未確認では planar 初期化/98 VRAM 書込みを行わず、gfx_started=0、既存 serial/再起動経路へ留める。安全条件を init/init_200 と故障出口に通す。
表示中の転送/palette/再 enter 失敗も stop→route00→既存 GUI 終了境界→面の再結線へ進む。使用中の GUI の下で寸法の違う BB へ即時に差し替えず、KAPI の void 型は維持する。
順番は **route00→ID/定数長/base/資源/写像確認→Command=0x0102 設定・生の 16bit 読戻し確認→ROM 採取・解読→A2 の legacy 無効化/解錠→main0〜4→基準値採取→mode 計算→共有列→必須 CHECK→初回転送/セル→route01**。A1 は本番の全経路から外す。
**N1**：資源確認後・A2 前に `pci_cfg_write16` で Command=0x0102 を一度書き、生の 16bit 読みで一致を確認する。冷起動時の 0x0106 をここで正し、不一致は既存失敗出口へ進む。BAR には書かず、Status の W1C を含む DWORD RMW はしない（`D/GF6200_4A_DESIGN.md:55,87`、`O/kdebug/gfinit_plan_c.c:230,272`）。
本番は既知 27 命令専用 runner。解読・整数 PLL・資源検査・A2 のハード手順・局所復元を共有し、recover の plan/node/capture/REC 実行器や kpeek を本番から呼ばない（`O/kdebug/gfinit_exec.c:80`、`gfinit.c:176`）。
PROM は BAR0 から既存の二重読み/header/checksum/既知 script 検査で採取する。x86 は解析せず、全段 hash 固定も追加しない。未知 ROM/命令・処理失敗は再試行せず S0 相当へ進む。
既存の命令/呼出し/反復/累積待ち上限を保ち、命令と wait の刻みに中断点を置く。診断の main 間承認待ちは本番に持ち込まない。
**時間（F11）**：devinit の同期停止は 1〜5 秒程度を仮置き（未実測の推測）、本番全体のソフト上限は既存 `GF_INIT_EXEC_US` の 60 秒を採用（`O/kdebug/gfinit_exec.h:4`）。ROM 採取前から基準値採取まで計時し、P6 で実測と GUI 表示までの総時間を記録する。期限超過は既存失敗出口へ進むが、戻らない MMIO に壁時計上限は保証できない。
配置は **`geforce.mod`＋核の薄い予約/復帰口**（U5）。下層/中層は `hwdev/gf/`、HAL は `gfx/`、診断 adapter は `kdebug/` と従来 hwdev.mod。本番は KDEBUG=0 で動く。
既存 loader/Cirrus 登録を使い、NULL 加速 callback と非NULL prepare を許す（`O/kernel/cirrus_module.c:22–43`）。kdebug 依存の hwdev.mod を名称変更するだけでは済ませない（`O/kernel/hwdev_module.c:87–96`）。
**診断 owner/lease は `kdebug/gfinit.c:70–74` のまま**（F7）。核から `int gf_production_selected(void)` 一つを export し、GFX=geforce の起動では gfinit の入口で拒否する。本番の成否や GUI 終了後にも拒否を保ち、診断と本番は設定変更＋再起動で入れ替える。S0/緊急復帰入口は止めない。
本番 context/復帰 callback は起動中保持して module を pin し、正常終了後も予約は保持する。owner の核移設、共通装置 registry、N1 lease の再設計は行わない。
P6 までは診断 A2/main の旧展開を比較用に残す。**runner の実機受入後、診断も同じ A2/main runner＋記録付き bus に移し、schedule.inc・emit.inc・*_expand.inc と専用の旧接続を削除する**（F4）。診断の段別承認/記録/中断/S0 は adapter に残し、C plan・A1/A4/VPLL/virtual が使う汎用機能は維持する。

### 4-3. 98 復帰と照合

0FACh は 8bit、GF=01、98=00、照合は下位 2bit。上位 bit を保存再書込みせず、既に 00 なら無書込み（`T/TASK_GEFORCE6200.md:75–86`）。
終了/障害/reboot 前は **局所 index/lock/窓復元→1850=0→1854=0→PMC140=0→route00→decode 安全確認→98 復元・予約セル往復・再描画**（`D/GF_STAGES_DESIGN.md:115–117`）。
identity/BAR が変わった時は危険な GF MMIO を省いても可能な route00 を試す。cfg50/54・1850/1854・Command の legacy 解放未確認なら 98 VRAM に触れない。失敗を記録して再起動へ進み、ログ失敗で戻しを止めない。
S0 は PLL/PFB/旧 timing の完全復元ではなく PARTIAL/S1 未保証を保つ。無応答 MMIO/CPU 停止は既存の物理リセット手順へ戻す。
表示中も Command=0102、cfg50/54・1850/1854=0、VGA は BAR0 別名を使い実 3Cxh I/O は使わない。98 の同期/31kHz を不要に送り直さない（`O/kdebug/gfinit_bus.c:91–99`）。CUI ログは 98 に残し、復帰後の文字/入力更新を確かめる。
本番は required。診断 C の record/required・除外ファイルと二段の照合は維持する。機能 CHECK の descriptor は `O/kdebug/gfinit_plan_c.c:214–275` から共有し、診断の VRAM 模様/全域比較/REC は本番にリンクしない。
必須値：50C mask=EFFFFFFF/expected=10000204、選択後の 508 mask=0007FFFF、578 mask=8000FFFF、580 bit8、524=初回&~50000、C040=初回値。
さらに CONFIG&3=2、START=0、CR00..18（最終 CR11=AB・CR17=C3）/19/1A/25/28/44/42/1B/20/47、CR33=初回&~3B、SR01=01、MISC=E3、GENERAL=20100130、DACCLK&101=1、TEST bit20。CR1A mask=D7、ほかの列挙 CR は FF。
route01 とセルは上層の切替後に判定し、共有列の終了条件に route01 を混ぜない。510/TEST sense/scratch、50C bit28・CR1A bit3/5・680A24 全bit の照合は復活させず、AR/DAC の副作用読取りも増やさない（`O/docs/POLICY_DEBUG.md:1585–1590`）。
record でも ID/BAR/legacy/範囲/route/セル/バス失敗は止める。必須 CHECK と任意観測を区別し、全レジスタ readback を追加しない。

## 5. 既存実装の扱いと機械的分割

AST で関数/文の範囲を得て対応表から機械切出しし、static・順序・条件コンパイル・著作権を保持する。まず同じ TU の元位置へ `.inc`、機械語一致と host の受入後に独立 `.c` 化する（`O/docs/POLICY_DEV.md:132–138`）。
第一段は同じ flags の objdump で命令と relocation の symbol/addend を比較する。独立 `.c` 化の最適化差は別差分で説明し、移動を新規実装に数えない。本起草ではビルド/試験はしない。

| 現行と抽出範囲 | 行き先・今回の扱い |
|---|---|
| `kdebug/gfinit_decode.c:12–65`・`gfinit_op/*_decode.inc`・bytes/tables の解読部分 | switch と既存 include 構成を保って共有。recover 型からの切離しは薄い接続だけ。27 本へ再分配しない。expand_step 等の旧展開は P6 までだけ存続。 |
| `gfinit_exec.c` 125 行（wait:16、control:27、run:80） | 必要な wait/control の範囲を同 TU で機械切出しし、既存意味を runner の手本にする。旧 plan executor の丸ごと移植や恒久的な二重実装はしない。 |
| `gfinit_pll.c` 246 行、helpers 56–77、parse 78–124、solve 125–191、sequence 192–246 | `gfinit_pll/{limits,parse,solve,sequence}.inc`→`hwdev/gf/pll/`。共通 solver は一つ。 |
| `gfinit_mode.c:33–111` calc（141 行のファイル内）：timing/pitch | timing は 42–77 の timing 文と 96 の config、pitch は 64/68/69 の pitch 文→各 `display/{timing,pitch}.c`。共有行は文境界で機械切出しする。 |
| 同 calc：format/extended | format=53/79–81/91、extended=85–90/93 の engine_ctrl/94–95/99–100→各 `display/{format,extended}.c`。各代入は一度だけ移す。 |
| 同 calc：cursor/fifo/start | cursor=92–93/98 の cursor 関連文、fifo=92 の burst/lwm、start=96 の start→各 `display/{cursor,fifo,start}.c`。 |
| 同 calc：DAC/output/PLL | dac_format=98 の general、output=101、PLL=104–110→各 `display/{dac_format,output,pll_program}.c`。ゼロ初期化・順序・依存を保存し、新規計算は無し（F9）。 |
| 同 calc の下処理と残り | 最初は対応する文区間を元位置の `.inc` として一度ずつ残す。独立 `.c` 化時に各機能を一つにまとめ、mode_bit と zero 初期化は共有、集約関数は呼出しだけ。palette:112–123 と模様 row:124–141 は診断に残す。 |
| `gfinit_resources.c` 211 行、36–107/109–144/146–164/166–211 | bounds/bars/windows/scan の機械分割→`hwdev/gf/resources/`。読取り検査を共有し、F6 の ID/定数長接続を加える。A1 sizing は診断専用。 |
| `hwdev/gf_load_state.c` 33 行・`generated/gf_mode_rows.inc` | 分割/複製無し。唯一の生成元 `tools/gfinit_mode_tables.py:29` を維持。blank/DPMS の modeset 順序もここに保持。 |
| `gfinit_plan_c.c` 529 行、baseline:159–186・checks:187–275 | 本番で共有する baseline/CHECK の区間だけ同 TU の `.inc` へ機械切出しし、共有値・descriptor を独立化する。診断の node 生成接続、446 の呼出し、rows/build/control/vram/color は元の場所に残す（N3）。 |
| `gfinit.c` 541 行、ROM:143–212 | 本番で共有する ROM の区間だけ機械切出しし、共有 parser は一つ。診断用出力/接続・lifetime/stages・owner/lease は元の場所に残す。P6 の A2/main 呼出し変更は維持する（N3）。 |
| `gf_stage_state.c` 73 行・`gfinit_bus.c` 211 行 | baseline 値採取と局所復元だけ必要範囲を共有化。診断 recover 型の adapter を残す。 |
| `recover_gf.c:125–129`・`recover_profile.c:42–71` | 本番が共有する戻し部品は quiesce と 98 セル手順の小さな二つ。記録/画面復帰 adapter は各層に置く。**recover.c の分割は GF_STAGES T3 に任せ、本票の作業・見積りに含めない**（F1、`D/GF_STAGES_DESIGN.md:162`）。 |
| `gfx/gfx_core.c` 1,251 行・既存 PEGC/Cirrus | gfx_core は §4 の局所変更だけ、分割無し。`backend_pegc.c` 1,215 行・`backend_cirrus.c` 606 行は参考として読む。 |

移した実体の旧コピーは消す。診断 A1/A2/main/A4/C-only、CLI、記録、C-only の常駐 ROM/plan/lease は必要な寿命を保つ。P6 の展開削除時にも他診断が使う実体を巻き込まない。
build/生成契約/source を読む試験/check_map/p2v 許可表/変異目印を追従する。GNU11・kstring・定数三層管理・物理変換（[C1–5]）と既存 KAPI 配置/cdecl（[ABI1–3]）を維持する。公開 ABI 更新が必要なら kapi.json・末尾追加・全再構築を P5 に含める。

## 6. 段の分解・並列割当て・受入

実装前に PM が機能表・契約・割当て・試験/実機受入・見積りをユーザーへ説明し了承を得る（`T/TASK_GF_DRIVER.md:32`）。この設計票作成は実装承認ではない。以下は将来の作業。
新規ファイルは約 500 行が目標で強制ではない。1,000 行超を触る担当は sol/astra、それ以外は terra 可。共有ヘッダ/dispatcher/生成器の担当は一人に固定する。

| 段 | 内容・触るファイルと規模見込み | 担当・並列条件 | 試験・受入 |
|---|---|---|---|
| P0 契約 | video_service.h/internal.h 計 100〜160 行、部品/文区間の対応表 | astra/sol、直列 | Trident fake で契約成立、ABI と選択門・失敗出口を確認 |
| P1 機械分割 | §5 の小ファイル `.inc`、元 `.c` と入力登録。後段含め一意の移動計 800〜1,350 行 | terra 可、TU ごと並列。大規模分割無し | objdump 一致、既存対象 host、GF 無し NP21/W の 98 退行 |
| P2 A/runner | op 27 本各 5〜30 行・計 300〜500、runner 120〜200、共有 decode/PLL/resources 接続 | terra で命令分担、dispatcher は sol。P0/P1 後 | 実 ROM fixture（27 種/193 命令）の旧展開との I/O trace 差分試験 1 本＋制御個別 case |
| P3 B | calc の機械分割各 5〜40 行程度、row adapter 100〜160、palette/blank/stop 各 20〜70、service 80〜140 | terra、P2 と並列可。共有列/C 接続は sol | 移動した各機能の既存試験、AR/DAC/LIVE/FP/PLL復元・独立期待列・CHECK |
| P4 起動/復帰 | devinit 100〜180、bus/基準値/ROM は移動主体、選択 export/核接続は数十行 | sol/astra 統合、部品は terra。P2/P3 後 | KDEBUG=0 fake 起動、初回 Command 書込みは 1 回・BAR 書込みは 0、0x0106→0x0102 の 16bit 確認と不一致時の停止、A2/main順・期限・各失敗出口、選択中 gfinit 拒否、再 devinit 無し |
| P5 HAL/配備 | backend 180〜260、present 60〜100、route 40〜70、module/設定/台帳接続 80〜130、gfx_core 局所 30〜60 行 | sol が接続、HAL/present は terra。P4 と fake 経由で並列可 | GUI=1拒否・GUI≠1許可（省略時0）、GUI 起動・CUI 描画アプリ起動の各場合で、起動 1 回につき最初の gfx_init で 1 回だけ devinit、dirty/pitch/palette、CLIENT は PEGC/GF 一方だけ、pc98 fallback、decode不明時98無書込み、shutdown→再init、部品欠落/KDEBUG=0。NP21/W は GF 無し拒否と98/PEGC/Cirrus退行 |
| P6 本番受入→診断統一→削除 | 下記実機 1 回。その後 A2/main の記録 bus adapter（80〜140 行目標）へ接続、旧展開と不要 glue を削除 | PM＋ユーザー立会い、整理は sol/terra。runner の実機合格が診断移行の前提 | 実機 5 場面と機械項目→保存した差分 trace/制御 case/診断段別承認・記録・中断/S0/C-only退行で統一を確認。旧展開削除までを完了条件にする |

F8：新規 host は実 ROM fixture の差分試験一つと REPEAT/SUB/CONDITION/MACRO/RESET/PLL の個別 case が中心。全命令へ一律 4 case を新設しない。skip/head、呼出し/反復上限、RESET 強制実行/184C 復元、PLL 26/7/1/1/2 と失敗点を重点化する。
既存試験は機能別の入口へ移し、fake bus/clock を共有する。P6 削除前に旧経路の trace を fixture 化し、手本由来の独立期待値と併用する。削除後も旧 executor を試験用コピーとして残さず、製品表から期待値を作って自己一致を主張しない。
各段は必要 build/対象 host、最終統合は PM の既存手順で一回。試験/fixture/台本/build 接続の新規・書換えは合計 600〜1,000 行目標。NP21/W を GF 映像の証拠にしない。
**立会いは本番だけ 1 回、目視 5 場面**（U7 決定）。診断 C は T2 で受入済みという PM 判断を引き継ぎ、解読と共有列を保持するため再受入の回を設けない。失敗修正後の追加立会いは必要になり得る。
当日は `GUI=0,GFX=geforce` で 98 CUI 起動→手動 GUI（GF①）→通常終了して 98 文字/入力更新（98②）→再 GUI、C 相当だけ（GF③）→既存中止経路または受入用打切りで 98 復帰（98④）→reboot 後の 98 CUI（98⑤）。危険なレジスタ値は注入しない。
機械項目は版/ROM/ID/BAR/Command/legacy、初回 Command 書込み 1 回・BAR 書込み 0、devinit 回数=1・再 GUI で A2/main 増加無し、CR52 非累積、必須 CHECK、palette/dirty の処理結果、route/セル、終端、devinit 実測秒数・初回 GUI 総時間。目視は同期・色・文字/マウス更新を判定し、レジスタ一致で代用しない。
無応答/同期外れ/98 復帰失敗なら中止し、既存 S0→reboot/物理復帰へ進む。ログ欠落・判定器エラーで戻しを止めず、現場で新しい修復列/失敗分類を追加しない。P6 の診断統一は host で確認した範囲を記録し、移行後診断の新たな実機合格を称さない。

## 7. 行数・容量の見込み

移動 M は元の一意な行を一度だけ数え、純増 0。新規 N は意味を書き換える行も含み、削除 D は移動元を除く置換旧行/廃止実体。以下は推測で、詰め書きや必要処理の削除を求めない。

| 区分 | 新規/書換え N | 移動 M | 置換/廃止 D |
|---|---:|---:|---:|
| A：27 run・runner/devinit・共有解読/PLL/resources 接続・P6 診断 adapter | 約 600〜1,000 | 500〜850 | 400〜650 |
| B：service・row adapter・palette・blank・stop（calc/CHECK/基準値は移動） | 300〜500 | 200〜300 | 40〜80 |
| OS32：HAL/present/route・予約/module/設定/台帳・共通ヘッダ | 500〜850 | 100〜200 | 20〜70 |
| **製品合計** | **約 1,400〜2,350** | **800〜1,350** | **460〜800** |
| host/fixture/受入資料/build・生成器接続（別枠） | **600〜1,000** | 既存 case の必要範囲のみ、実装時計数 | 移動元以外の旧比較/glue を実装時計数 |

N2：A の内訳は run 計 300〜500＋runner 120〜200＋devinit 100〜180＋P6 adapter 80〜140＝600〜1,020 行で、接続を各項に含め、概算を約 600〜1,000 行とする。製品 N は B/OS32 を加え約 1,400〜2,350 行。
N3：M は本番共有区間で再見積り。A は ROM 143–212 を含む解読/PLL/資源等で 500〜850、B は calc 33–111・baseline 159–186・checks 187–275 の計 196 行を主に、必要な値採取等を含め 200〜300、OS32 は従前の 100〜200、計 800〜1,350 行。診断専用の rows/build/control/vram/color・lifetime/stages は M に含めない。
recover.c/gfx_core.c の大規模分割は計上しない。B の calc 分割を新規に数えず、A の削除には P6 の schedule/emit/*_expand と専用旧接続を含む。必要な解読/共有計算/他診断の実体は削除数に含めない。最終 diff で N/D/M を再計数する。
CLIENT は PEGC/GF のどちらか一つの 300 KiB。BAR1 の DISPLAY は MMIO で RAM の CLIENT と別。初期化中 ROM は最大既存 GF_ROM_MAX、runner の有限作業域を使い、limits/基準値を値コピーした後に ROM と作業域を解放する。本番再 modeset は ROM に依存しない。
常駐は palette 768 B＋mode/基準値/資源など数 KiB の見込み。診断の常駐 ROM/plan/lease は別の起動で既存寿命を維持する。image/mem・loader span は未計測で、P4/P5 に実測し上限超過は PM に報告する。
27 run だけ新設、解読/整数 solver/生成列/計算の共有、1 mode・加速無し・CLI 新設無しで増加を抑える。Cirrus 全体約 939 行を規模の比較材料とする（`opus_r1.md:14`）。行数目標を強制上限にしない。

## 8. ユーザーに決めてもらう所

**無し。** PM は U1=640×480×256、U2=GUI≠1（省略時0）で GFX=geforce の明示選択、U3=ID 限定の定数長/BAR 無書込み、U4=palette/blank/start=0/cursor 非表示、U5=geforce.mod、U6=専用 runner＋受入後の診断統一/展開削除、U7=本番 1 回・5 場面を決定済み。
本票はその決定を反映した実装説明の材料であり、実装着手前の PM の詳細説明・了承は別途必要。本作業は設計票と変更一覧の作成だけで、コード変更・ビルド・試験・NP21/W・実機操作は未実施。
