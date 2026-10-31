# sdk/allocator/nano_adapter.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: API_CONTRACTS (D1 で移した)
ソース: `sdk/allocator/nano_adapter.c`

C (fixture) は OP_INIT 成功後に `os32_gui_retry_enable()` を呼ぶ。
TRIM 周に `os32_gui_trim_serve(epoch, hook)` を1回呼び、nano trim → hook → DONE と進める。
戻り値は nano の返却 page 数、busy / in_trim は0で hook / DONE も呼ばない。
C の `retry_count / retry_ok_count / retry_refused_count / trim_serve_count / hook_pages_total` は

`os32_gui_retry_stats()`

C の calloc 積 overflow も対象外。realloc は1回目失敗で旧内容を保ち、

成功時だけ copy と旧解放を行う。C の size 0 は従来どおり nano に任せ、再試行しない
(`malloc(0)`・`calloc(0,x)`・`calloc(x,0)`・`realloc(NULL,0)` は確保可能なら非 NULL、
`realloc(p,0)` は旧領域を解放して NULL)。
