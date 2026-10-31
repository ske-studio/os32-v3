# kdebug/gfinit_decode/ — 設計
> 状態: **現行 (2026-10-31)** — 出所: GF6200_4A_DESIGN.md (D1 で移した)
ソース: `kdebug/gfinit_decode/`

### tables.inc

ソース: `kdebug/gfinit_decode/tables.inc`

- 表領域は半開区間 `[start, start+n×size)` とする。IO flag 表 `[0x5DAA, 0x5DAA+8×9)` の終端0x5DF2は表に含めず、SUB8先頭として許可する。
