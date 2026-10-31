# exec/exec_heap.c — 実行コンテキストのヒープ管理

> 状態: **現行 (2026-10-31)**

常駐側とユーザー側の exec ヒープを管理する。
親文脈の管理状態の保存と復元も提供する。

## 落とし穴

- 子の終了後に親ヒープを初期化し直すと、先頭のブロックヘッダが上書きされる。
  `exec/exec.c:1019` は `exec_heap_restore_state` を使い、ヒープの中身を保つ
  (`exec/exec_heap.c:237`)。復帰に `exec_heap_init_at` を使わない。
  親子の実行文脈は `tools/tests/test_exec_r1.py` で確認する。

- newlib の sbrk と exec_heap が同じ領域から伸びると互いの割当を上書きする。
  常駐シェルは画像と sbrk の上限を exec_heap の開始位置で区切る
  (`exec/exec.c:1542`, `include/memmap.h:292`)。境界は定数を参照する。
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
