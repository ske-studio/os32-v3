# sys/kern/mm/shm.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§2-4・§2-5 e10a) の契約を今のソースと照らして要約
ソース: `sys/kern/mm/shm.c`・`sys/kern/mm/shm.h`

共有メモリ (SHM) のブロックの確保・lock・解放と、プログラム終了時の回収。SHM の全ページは起動時から全アプリに `USER` で見え (`docs/design/mm/paging_map.md`)、ここで変えるのは RW ビットだけ。

## 契約

| 操作 | 結果のページ属性 | 状態 |
|---|---|---|
| `shm_lock` | `USER|RO` | LOCKED |
| `shm_free` | `USER|RW` | FREE、所有者・span を 0 |
| `shm_free_owned(owner)` | 対象ブロックを `USER|RW` | FREE (その所有者の分だけ) |
| `shm_cleanup_all` | 予約以外の使用中ブロックを `USER|RW` | FREE |

- 属性の変更はすべて `paging_shm_set_rw` を通し、汎用の map 口を使わない。どれも PCD を付けない (WB)。
- `paging_shm_set_rw` が拒否した (USER の欠落など) ブロックは状態を変えずに残す (lock・free は -1、回収は飛ばす)。
- USER からの lock・free は自分の確保したブロックだけ (`shm_block_owner` と呼び手の所有者の照合)。他の所有者のブロックは -1 で何も変えない。カーネル・WM の呼び出しは照合しない。
- span が管理表の外へ出る・使用中でないブロックを含む lock は -1。
- DB・GUI の予約ブロック (`SHM_RESERVED`) は回収の対象外で、最初の確保もそこを避ける。
- `shm_free_owned` の後 (exec の終了) でも次のアプリが SHM の全ページに書けること。

## 検査

`tools/tests/test_shm_user.py`。
