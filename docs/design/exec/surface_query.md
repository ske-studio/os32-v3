# sys/kern/exec/surface_query.c — 設計
> 状態: **現行 (2026-10-30)** — 出所: GF_DRIVER_DESIGN (2026-10-29 確定、D0 で移した)
ソース: `sys/kern/exec/surface_query.c`

SURFACE の GF 識別定数は既存 ID の末尾へ追加し、`sys/kern/mm/pgalloc.h`・`sdk/include/os32/os32_surface.h` の `OS32_SURFACE_CIRRUS`・`sys/kern/exec/surface_query.c` を追従する。

## 授権と ref の照合 (`surface_query_authorize`・`surface_query_refs`)

> 出所: TASK_T2D_T2H (§2-2・§2-5 e1・e3) の契約を今のソースと照らして要約 (2026-11-18)

- 呼び手は保存された由来が `CALLER_USER` で、slot が RUNNING のときだけ (停めた親は対象外)。IRQ・例外の中は INVAL。
- 役割ごとの授権 (GUI = `con_sink_is_enabled`、GFX = OS32X ヘッダの `OS32X_FLAG_GFX`、全画面の所有者 = `appslot_gfx_owner`):

| 役割 | 許す | 拒否 |
|---|---|---|
| CLIENT | CUI のアプリ、GUI の GFX 未宣言アプリ、GUI の全画面の所有者 | GUI で GFX を宣言したが全画面の所有者でないアプリ |
| DISPLAY | CUI の GFX アプリ、GUI の全画面の所有者 | GFX 未宣言、GUI の通常アプリ |
| TVRAM | CUI のアプリ | GUI 中 |

- CLIENT・DISPLAY の backend は既知の範囲だけ、TVRAM の backend は 0 だけ。
- query は値の写しだけで lease 数を増やさない。PC98 の DISPLAY は 4 記録を 1 つの snapshot で返し、それ以外は 1 記録。各記録の `plane_offset + pitch×height` が面の bytes に収まらなければ INVAL。
- ref の照合は束の全部 (範囲外・世代 0・重複) を先に検査してから世代を比べる。sid が今の束と違い、かつその sid が今も有効な面なら INVAL (別の束)。それ以外の食い違いと古い世代は STALE。
- 公開の誤りコードは `surface_query_error` が lease の内部値を写す (STALE → `OS32_ERR_STALE`、FULL → `OS32_ERR_FULL`、NOMEM → `OS32_ERR_NOSPC`、ほかは `OS32_ERR_INVAL`)。
