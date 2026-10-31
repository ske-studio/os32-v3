# kdebug/gfinit_plan_c.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `kdebug/gfinit_plan_c.c`

record でも ID/BAR/legacy/範囲/route/セル/バス失敗は止める。必須 CHECK と任意観測を区別し、全レジスタ readback を追加しない。
診断の node 生成接続、446 の呼出し、rows/build/control/vram/color は元の場所に残す（N3）。

## 落とし穴

- **必須照合と観測**: 書込み後の全レジスタを必須照合すると、読戻し値が異なる箇所で停止し、残りを調べられない。
  `kdebug/gfinit_plan_c.c:109` はルールに verify mask を持たせ、必須 CHECK と記録を区別する。
  診断では `--verify record|required` と実行時の除外ファイルを選べる (`userland/shell/cmd_gfinit.c:97`)。
  除外は実際に読戻しを保証できない範囲に限り、`tools/tests/gfinit_plan_c_host.c` で計画の照合を検査する。

## GF6200_4C_DESIGN

VRAMはQ→Pで停止後・Dより前にBAR1+[0,4B000h)の307,200 Bを予約してCPU転送。1行640 Bの固定試験データを480行、全行書込み後に全域比較、posted write排出は既存の安全なGPU読取り。VRAM容量/窓の下限と書込範囲をplanで確定（`docs/design/kdebug/gfinit_plan.md`）。

HWDEV側 `gfinit_plan` に A2+main+固定C節を最初からbind。Cは入口CHECK→Q→P→VRAM→S/D→L1/2/3→C→R→出口CHECK→NONE、成功はPAUSED。失敗は既存trial_end、途中再開なし（`kdebug/gfinit.c:298–357`）。

307,200 BをDWORDごとのplan/RECへ展開しない。固定640 B行の有限転送/比較を内部操作として追加し、方向ごとにgf_bus BEGIN/ENDを1組（転送全域で1組・比較全域で1組、先頭・長さ・方向、最初の不一致番地/値だけ記録）、途中の既存safe point・窓復元を持つ。DAC768 byteも固定有限列。汎用bulk APIにはしない。

`gfinit_mode_row` の 640 B 行を480行、A4と同じDWORD窓アクセスで処理し、窓/PTE/IRQ復元後の行間に既存safe pointを置く。全行転送後は既存read-safeなPFB容量レジスタの読取りでposted writeを排出する。

- AR の 21 行と gate 3 行は専用の C 操作とし、

- DAC の mask/index/data 3 行は固定 1 操作にまとめ、

## GF_STAGES_DESIGN

再Cは保持した作業域でC planを組み直し、flow容量を再利用する。S0で旧planの結合を外してから再bindするが、裏の確保領域を解放しない（`recover.c:1805`）。

同一セッションの初回はprefix＋C、再Cは保存limitsと空の計画からCだけを組む。同じC追加関数を使う。

再Cのsaved式は常駐基準値からliteralへ解決し、初回CR52/C040を再採取で上書きしない。現在値が要るLIVEのRMW・FP条件は現在値を読み、基準値で置き換えない。

初期化ROMの再decode・A2/mainの再実行はしない。C-onlyでは開始前にID/BAR/範囲を確認し、旧snapshotに依存する入口BAR期待値とsaved operandsを常駐値から設定する。

VRAMは `gfinit_plan_c.c:411` の `gfinit_plan_c_vram` の全域走査完了とデータ不一致を区別して `mode_vram_access`（`recover.c:2156`）へ返す。
