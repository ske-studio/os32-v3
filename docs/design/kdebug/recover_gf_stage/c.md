# kdebug/recover_gf_stage/c.inc — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4C_DESIGN.md・GF_STAGES_DESIGN.md (D1 で移した)
ソース: `kdebug/recover_gf_stage/c.inc`

## GF6200_4C_DESIGN

| 番地・値（省略した接頭辞は同一行内のみ） |
| --- |
| 出力中の98セル往復→0FACh←01h（8bit、mask03hで01）→出口CHECK→PAUSED |

SR/CR/GR/AR・DAC dataの副作用は型で扱い、領域一括MMIO許可にしない（`docs/decisions/realhw/REALHW_RECOVER_DESIGN.md:115–120`）。

必須CHECKは50C全bit、524の計算値、580 bit8、C040復元値、CONFIG&3/START、標準CR/形式/pitch/blank/sync・DACCLK&101、cfg/route。TEST_CONTROLはbit20だけ比較しsenseを除外、未知・未使用scratchは記録のみ。ARはFF操作付き専用列、DACは書込み専用列としread_safeな通常MMIOへ偽装しない。

0FAChは現行target検査で禁止（`kdebug/recover.c:605–608`）。Cの固定route節だけ既存route callbackを使う専用操作を結線し、01/読戻しmask03、終端00を定義。任意I/O opは足さない。S0の98セル部分を出力中CHECKでも再利用する（§5）。

- `recover_plan_access` が固定範囲・方向の型を扱い、`recover_ops.mode_vram` へ渡す。正常時はgf_busが方向ごとにBEGIN/ENDの計4行（bar/off/len/dir）。比較は不一致後も全域を読み、最初のbyteのBAR1相対off/期待値/実値だけをENDに保持し、既存のREC_VERIFY失敗出口へ進む。

出口 CHECK は §§3–4 の必須値と 510 の読取りを保持し、係数は 50C 選択後だけ比較する。CR11 は最終 AB、CR17 は C3、SR01 は 01 を比較する。

`recover.c` の `mode_check_access` に読取り・mask 比較・項目ごとの CHECK REC・不一致時の既存 REC_VERIFY 失敗出口をまとめる。

- 待ち 3 本は専用の型と `recover_ops.mode_wait` から既存 `recover_delay_ms` の 1ms 分割・safe point へ結ぶ。手本の順序は nouveau c03c1a1 MIT `dispnv04/dfp.c:93–110` (FP_ON 時の OFF→50ms)、`crtc.c:175–234` (P/C の SR01→10ms→CR17)。実行せず leave する CHECK/待ちは拒否する。共有 callback/型の契約 CRC・layout は生成器で追随する。

## GF_STAGES_DESIGN

| 対象 | record | required／除外ファイル |
| --- | --- | --- |
| 入口ID/BAR/Command/legacy・窓/バス・範囲・98セル・route・S0 | 必ず検査し、失敗は停止／S0 | 同じ。除外不可。 |
| VRAM全域比較のデータ不一致 | 不一致の位置・expected・actualを記録して後続CHECKへ進む。範囲/BAR・読書き失敗は停止／S0 | 全域比較を実施し、不一致でS0。除外不可。 |
| mode機能の必須CHECK | 安全に読める実値・expected・mask・不一致を記録し、不一致だけでは止めない | 下記の最終時点で照合。不一致でS0。除外ファイルでも必須CHECK自体は残す。 |
| 書込み直後の任意照合、未知scratch・510・TEST sense | 記録のみ。副作用のある読取りは追加しない | 記録のみ。既存の除外指定も任意照合だけに効く。 |

`mode_check_access`（`recover.c:2264`）へ渡す。

先に不一致があっても後のバス失敗を隠さず、完了した比較の不一致だけrecordで非停止にする。既存の失敗分類を使う。
