# hwdev/gf/display/fifo.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `hwdev/gf/display/fifo.c`

FIFO は表示用の設定を扱い、描画エンジンのコマンド FIFO は作らない。
| 機能・ファイル語幹 | 中身・初版の境界 | nouveau（`N/` 以下） | VBE 対応 |
|---|---|---|---|
| 表示 FIFO `fifo` | NV44A burst=4、lwm=C0、CR1B/20/47 | `dispnv04/arb.c:239–264`、`hw.c:712` | 02 の内部 |

## 移し替えた設計

FIFOはNV44A分岐のburst=4/lwm=C0をstateに先置き。
