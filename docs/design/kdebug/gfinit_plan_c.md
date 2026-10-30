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
