# sys/platform/pc98/console.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (T2e の KAPI の範囲検査の欠落の修正) の契約を今のソースと照らして要約
ソース: `sys/platform/pc98/console.c`

PC-98 のテキスト VRAM (TVRAM) へのセル単位の読み書き。KAPI の console 系の座標がそのまま届くので、TVRAM の外を指す番地を作らない。

## 座標の範囲

| 関数 | 受け付ける範囲 | 外れたとき |
|---|---|---|
| `con_backend_putchar(x, y, ch, color)` | `0 <= x < TVRAM_COLS`、`0 <= y < TVRAM_ROWS` | 何も書かずに戻る |
| `con_backend_readchar(x, y, code, attr)` | 同上 | `*code`・`*attr` を変えず、TVRAM を読まない |
| `con_backend_putkanji(x, y, jis, color)` | `0 <= x < TVRAM_COLS - 1` (右半分のセルも収まる)、`0 <= y < TVRAM_ROWS` | 何も書かずに戻る |

- 範囲の検査は番地の計算 (`y * TVRAM_BPR + x * TVRAM_CELL_BYTES`) の**前**に行う。負の値・`INT_MIN`/`INT_MAX`・u32 の番地が周回する大きい値 (x = 0x40000000、y = 0x08000000) も上の比較で弾く。
- 範囲の中なら先頭セル・最終セル・最終行の漢字 2 セルとも書ける。

## 検査

`tools/tests/test_kapi_bounds.py` が実 `sys/platform/pc98/console.c` を ILP32 で組み、TVRAM の両面とその間・外側の番兵、アクセス回数で確かめる。
