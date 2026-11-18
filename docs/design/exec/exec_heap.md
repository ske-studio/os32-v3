# exec/exec_heap.c — 実行コンテキストのヒープ管理

> 状態: **現行 (2026-10-31)**

常駐側とユーザー側の exec ヒープを管理する。
親文脈の管理状態の保存と復元も提供する。

## 落とし穴

- 子の終了後に親ヒープを初期化し直すと、先頭のブロックヘッダが上書きされる。
  `exec/exec.c` は `exec_heap_restore_state` を使い、ヒープの中身を保つ
  (`exec/exec_heap.c`)。復帰に `exec_heap_init_at` を使わない。
  親子の実行文脈は `tools/tests/test_exec_r1.py` で確認する。

- newlib の sbrk と exec_heap が同じ領域から伸びると互いの割当を上書きする。
  常駐シェルは画像と sbrk の上限を exec_heap の開始位置で区切る
  (`exec/exec.c`, `include/memmap.h`)。境界は定数を参照する。
  メモリ配置の回帰は `make check-memory-host` で検査する。

## 内部設計と契約 (D1)

シェルの 2 ヒープは API 2・供給元 1

| 指摘 | 答え |
|---|---|
| ⑧ 既定 exec_heap の先取り | `heap_size == 0` は MEM_EXEC_HEAP_MIN から始め、失敗時にページ単位で伸ばす。sbrk も張ったぶんだけ。**伸ばす経路 (通常文脈の KAPI、大きな塊は map/unmap) は §3-5 R2** |

ヒープ末尾に続けて `mem_map`
`exec_heap`
も同じ形で伸ばす。

> [要確認] 現行 exec/exec_heap.c は appmem_map() 経由で伸長し、シェル用固定ヒープとは経路が異なる。

## USER の exec_heap (`exec_heap_user_*`)

> 出所: TASK_T2D_T2H (§3-4・§3-5 f9〜f10) の契約を今のソースと照らして要約 (2026-11-18)

- **振り分けは保存された呼び手の由来**で決める。`wrap_mem_alloc`・`wrap_mem_free` (KAPI 生成) は `caller_access_get` が `CALLER_USER` を返したときだけ当該 AS の `exec_heap_user_alloc`・`exec_heap_user_free` へ渡し、WM・TRUSTED・由来を取れないときは常駐の単一 KHeap (`exec_heap_alloc`・`exec_heap_free`) へ渡す。現在の CR3 や CPL で推測しない。両方のヒープをまたいで free を渡さない。
- USER の arena は AS の extent 表 (`docs/design/exec/appmem.md`) の EXEC_INITIAL・EXEC_ARENA の行で、AS に KHeap を常駐させない。確保・解放のたびに候補 arena を検証してから一時の KHeap view を作る (`user_view`)。検証: 行が `MEM_EXEC_HEAP_BASE` 以上・`guard_b` 以下・ページ整列、全ページが当該 AS で書ける、ブロックの頭が整列・magic が USED/FREE・size が整列で arena の中・次の頭が単調に進んで arena の終端にちょうど届く。USER が書き換えられる BlkHdr をカーネルが辿る前に必ずこの検査を通す。
- 検証に落ちた・double free・ブロックの頭でない pointer の free・ANON や LARGE の中を指す free は、その AS の heap を以後無効にする (`exec_heap_used = ~0`)。以後の alloc は NULL、free は何もしない。他の所有者のページは変えない。
- **要求 ≥ 64 KiB** (`MEM_EXEC_HEAP_MIN`、丸める前の要求サイズで判定) は TOPDOWN で EXEC_LARGE を map して直接返す。作れなければ理由を問わず既存の arena に収まるときだけ使い、伸長しない。LARGE は併合しない・free で即 unmap・teardown で回収する。LARGE の double free は、未使用の VA なら無視、ANON の中なら無効化、同じ base に再確保されていれば新しい割当を free する。
- 小要求で収まる arena が無ければ `max(page_up(size + ヘッダ), 64 KiB)` を現在の exec_heap 端に EXACT で伸ばし、衝突したら TOPDOWN で別の arena を作る。publish で隣の ARENA と併合されうるので、作った後に view を取り直す。
- `exec_heap_user_trim` は当該 AS が active なときだけ動き、先に**全 arena** (INITIAL を含む) を検証する。ARENA の末尾が FREE のブロックなら、その頭と最小の中身を残してページ単位で末尾を返す (`appmem_exec_trim`)。INITIAL は返さない。返却に失敗したら header と使用量を変えずに止める。返したページ数を返す。
- IRQ・例外の中、毒状態の AS では確保・解放・trim をしない。

検査は `tools/tests/test_appmem_map.py` (実 exec_heap・kmalloc・caller_access・生成 KAPI wrapper を同じ ILP32 足場で連結)。
