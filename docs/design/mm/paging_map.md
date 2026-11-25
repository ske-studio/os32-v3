# sys/kern/mm/paging_map.inc — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§2-4・§2-5 e10a) の契約を今のソースと照らして要約
ソース: `sys/kern/mm/paging_map.inc` (`sys/kern/mm/paging.c` が取り込む)

master の共有 PT を書く汎用口と、SHM・トランポリンの USER を起動時に一度だけ立てる口、SHM の RW だけを切り替える専用口を持つ。

## 汎用口は live AS の後に USER を立てない

- `paging_set_page`・`paging_map_range`・`paging_map_phys` は、AS が 1 つでも生きている (`live_addrspaces` > 0) 間は `PTE_USER` を含む要求を -1 で拒否する。supervisor の写像 (装置の alias など) は従来どおり。
- 通常の DEVICE alias は supervisor のまま。共有 PT への USER の追加は、下の起動時の口と V86 の専用 session の口だけ。

## 起動時の共有 USER (`paging_boot_user_shared`)

- 呼ぶのは `shm_init` の**後**、最初の AS を作る**前**に 1 回だけ (`sys/platform/pc98/kernel_boot_memory.inc`。失敗なら起動を止める)。2 回目・AS が生きている・paging が無効・トランポリンが非整列やカーネル像の外なら -1。
- SHM 全域 (`MEM_SHM_BASE` から `MEM_SHM_SIZE`) を `USER|RW` (WB、PCD なし)、トランポリンのページを `USER|RO` にする。以後の起動処理は SHM を supervisor に戻さない。exec ごとの共有 map は持たない。

## SHM の RW 切り替え (`paging_shm_set_rw`)

- 範囲は SHM の中・ページ整列・空でないこと。**全ページを先に検査**してから書く: PDE が static の共有 PT を指す (PS なし)・PTE が恒等で PRESENT・USER が立っている。どれかが外れたら -1 で何も変えない。USER の欠落は `paging_shm_user_missing_count` に数え、汎用口で USER を立て直して救わない (不変条件の違反として検出する)。
- 書くのは RW ビットだけで、USER は保ち PCD は付けない。監査用の RO 表 (`shm_audit_ro`) を同じ区間で更新し、最後に TLB を同期する (他の AS は次の CR3 ロードで同じ共有 PT を見る)。PDE の USER は保つ。

## 検査

`tools/tests/test_shm_user.py` が実 `sys/kern/mm/paging.c`・`sys/kern/mm/pgalloc.c`・`sys/kern/mm/shm.c`・V86 の写像部を ILP32 で動かし、2 つの AS で SHM の lock・free・回収後の USER/RW/WB、2 本目の AS からの書込み、USER 欠落時の無変更拒否、live AS 中の汎用口の拒否、起動口の 1 回性と順序を確かめる。
