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
