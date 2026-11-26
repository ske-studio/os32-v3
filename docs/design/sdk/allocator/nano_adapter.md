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

## arena と morecore・大きな塊・trim

> 出所: TASK_T2D_T2H (§3-3・§3-4・§3-5 f1b・f6〜f8・f11) の契約を今のソースと照らして要約 (2026-11-18)

newlib nano (私有化した member) の上に SDK の adapter を被せ、複数の arena へ振り分ける。システムの libc.a は手で書き換えない。入力の台帳は `tools/check_nano_inputs.py` が検査する。

### morecore (`os32_nano_morecore`)

- arena ごとに `initial`・`brk`・`mapped_end`・`limit` を持つ。break を持つのは arena (primary は CRT と共有) だけで、CRT の `_sbrk` は primary の morecore へ委ね、別の break を持たない。
- 負の増分: 減らす量を符号なしで求め (`INT_MIN` でも符号の反転をしない)、`initial` より下へは下げられない。下げるのは break だけで `mapped_end` は変えない。
- 正の増分: `brk + incr` の桁あふれ・`limit` 超えは失敗。`mapped_end` を超えるなら末尾をページへ切り上げ (切り上げの桁あふれ・切り上げ後の `limit` 超えも失敗)、足りない分だけを `grow_exact` で**ちょうど** `[mapped_end, end)` に map する。USER は `mem_map(..., OS32_MEM_MAP_EXACT)` で、返った番地が違えば失敗。成功したときだけ `mapped_end` と break を進め、旧 break を返す。非連続の領域を成功として返さない。
- 失敗は `ENOMEM` と `(void *)-1` で、状態を変えない。
- resident (`OS32_CRT_RESIDENT`) は `grow_exact` を持たず、`limit` = `mapped_end` = 起動時の `sbrk_heap_limit` の固定上限。`mem_map` へ落ちない。USER では `sbrk_heap_limit` は初期 `mapped_end` の引渡し値で、実行中の天井ではない。`sbrk_heap_limit` が整列した `_end` より小さければ以後の全 sbrk が `ENOMEM`。

### arena の選択と再入

- malloc の 8 入口 (`malloc`・`free`・`calloc`・`realloc` と `_malloc_r` 等の reentrant 版) を全部この adapter へ結線する。未結線の memalign・valloc・mallinfo・usable-size などはリンクで拒否する。
- 入るときに busy を立て、選んだ arena の nano 状態 (`free_list`・`sbrk_start`・統計) を読み込み、出るときに書き戻す。busy 中の再入 (callback からの malloc、busy 中の arena 選択、busy 外の morecore) は `ENOMEM` で拒否する。単一の free list に別の arena を偽装連結しない。
- 小要求は primary から順に既存の arena を試し、どれにも入らなければ flags 0 の `mem_map` で副 arena を作る (最小 64 KiB、管理ページ 1 枚 + nano の頭。圧迫時は必要量だけで再試行)。arena の記録は各 arena の先頭ページに置く。
- free・realloc は pointer を含む arena を `[sbrk_start, brk)` で探す。境界でちょうど隣り合う chunk を別 arena のものとして扱わず、併合もしない。arena の live 数が 0 になった副 arena は list から外して unmap し、unmap が失敗したら list へ戻す。primary は返さない。
- realloc で同じ arena に入らなければ、別の arena に確保 → 必要量を写す → 旧領域を free。失敗したら旧領域と内容をそのまま残す。

### 大きな塊 (要求 ≥ 65536 B、`OS32_NANO_LARGE`)

- 要求サイズ (calloc は `n * size`、積の桁あふれは先に検査) が 65536 以上なら TOPDOWN の `mem_map` で直接確保し、先頭に頭 (base・map の bytes・要求・種別・magic・整列 8) を置く。頭 + 要求 + ページ切り上げの各段で桁あふれを検査する。calloc の 0 埋めは新しいページが 0 であることに頼る。
- free は一覧と完全に一致する payload だけを大きな塊として扱い、頭を検証してから即 unmap する。unmap の失敗では一覧から外さない。任意の pointer の頭は読まない。
- 小 ↔ 大の realloc は新しく確保 → 写す → 旧を返す。失敗で旧の内容を保つ。

### trim (`os32_nano_trim`、USER だけ)

- busy 中・未初期化なら 0。各 arena で、保存した free list の末尾の chunk が break に接していれば、`page_up(chunk + 12)` (頭と次参照に要る分) を残して以降のページを unmap する。primary は image の端ページも残す。
- arena ごとに unmap が成功してから size・break・`mapped_end` を確定する。失敗した arena は状態を変えず、走査は続けて成功したページ数を合計する。live の chunk・非選択の arena の生きている chunk は読まない・動かさない。空の副 arena は丸ごと返す。

検査は `test_nano_adapter.py (旧版 f86f6c836656)` (実 nano の member をリンクし、穴の再利用・整列の追加取得・EXACT 失敗・副 arena・跨ぎ realloc・trim と、リンクの負例)。
