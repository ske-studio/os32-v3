# sys/kern/mm/paging_app.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_T2D_T2H (§3-2・§3-5 f3〜f5) の契約を今のソースと照らして要約
ソース: `sys/kern/mm/paging_app.c`・`sys/kern/mm/paging_app.h`

アプリ帯 (`MEM_APP_BAND_BASE` から `MEM_APP_BAND_MAX_PDES` 枚の PDE) の私有 PT に、map の取引でデータページを張り、unmap の取引で外す。extent 表の計画は `sys/kern/exec/appmem.c` (設計は `docs/design/exec/appmem.md`) が持ち、ここは PTE・PDE・物理ページの順序だけを持つ。

## 文脈 (`paging_app_context`)

次のどれかに当たれば偽 (呼び手は EINVAL):

- IRQ・例外の中 (`paging_irq_ops` を数える)。IF=0。
- AS が毒状態、PD が無い・非整列・所有者違い、lease 窓の先頭 PT (`lease_pt_phys[0]`) が無い。
- `app_pde` が `APP_BAND_PDE` でない、`app_pde_count` が `MEM_APP_BAND_MAX_PDES` でない。
- 現在の CR3 が master でも当該 AS でもない。
- アプリ帯の各 PDE が `app_pt_phys[]` と食い違う (PT が無いのに PDE がある、PT の所有者違い、PDE が `PT|RW` (+USER・A・D) でない)。

## map: stage → commit / abort

- `paging_app_stage(tx, as, base, end)`: 範囲はページ整列・帯の中。**最初の確保の前に**範囲の PTE が全部 0 であることを確かめ、非 0 (PRESENT・NP・frame 無しを問わない) が一つでもあれば EINVAL で何も確保しない。
- 足りない PT を 1 枚ずつ確保してゼロ化し、`tx->pending[]` (64 本・256 B、呼び手のスタック上) に控える。次にデータを 1 枚ずつ確保・ゼロ化し、PTE に `frame|RW|USER` を **PRESENT なし**で書く。この NP の PTE が失敗時の返却表で、別の配列を持たない。確保とゼロ化は IF=1 のまま行う。
- PT・データのどれかが足りなければ ENOSPC (`paging_app_pt_nospc_count`・`paging_app_data_nospc_count` で区別)。返却は**データが先、PT が後** (`paging_app_abort`)。失敗の後には確保が一つも残らない。
- 全確保の成功後、新しい PT の PTE だけ PRESENT にする (PDE がまだ無いので見えない)。既存 PT の PTE は commit まで NP のまま。
- `paging_app_commit`: 呼び手が IRQ を保存した区間で、既存 PT の PTE を PRESENT にし、新しい PT の PDE を `PT|RW|USER` で張り (既存 PT の PDE には USER を足す)、`app_pt_phys[]` を確定する。確保・解放はしない。commit の後の abort は公開済みの PTE と所有を変えない。
- active な AS なら呼び手 (`appmem_map`) が extent 公開の後に CR3 を 1 回再ロードする。非 active の AS は次の CR3 ロードで反映される。

## unmap: prepare → withdraw → free

- `paging_app_unmap_prepare`: 書く前に全ページを検査する。PT が無い・PTE の下位ビットが A/D を除いて `RW|USER|PRESENT` 以外 (0・NP・RO・PS・PCD・PWT)・所有者違い・生きている SURFACE の物理 (lease の対象) なら EINVAL で、`paging_app_unmap_reject_count` を増やす。範囲の外に PTE が残らない PT を「空」として記録する。**`app_pt_phys[0]` は空でも残す** (`sys/kern/mm/shlib.c` が使う)。
- `paging_app_unmap_withdraw` (呼び手が IRQ を保存): 空の PT は PDE を 0 にするだけで中の PTE は触らない。残る PT の対象 PTE だけ NP にする。active なら CR3 を 1 回再ロード。IRQ 区間の作業量は残る PT の対象ページ数で抑える。
- `paging_app_unmap_free` (IF=1): 対象 PTE を NP にし、frame を 1 枚ずつ所有者付きで返してから PTE を 0 にする。次に空の PT を返して `app_pt_phys[k]` を 0 にする。順序は **NP → TLB 同期 → データ返却 → PT 返却**。

## 返却の失敗と毒状態

- 検査の後の返却 (`pgalloc_free_n_owner`) が失敗したら `paging_app_bad_free_count` を増やし、AS を毒状態にする (`paging_app_poison` → `paging_addrspace_poison`)。失敗した frame は PTE に残して診断に使い、途中まで返したまま成功を返さない。
- 毒状態の AS は live の数から外れ、対象の slot に中断要求を立てる。以後の map・unmap・destroy・lease の口は文脈の検査で拒否し、USER へ戻る前に kill される。teardown はその所有者のページを返し直さず、残りを数えてシリアルに 1 行出す。

## 検査

`test_appmem_map.py (旧版 8180480a2758)` が実 `sys/kern/mm/paging_app.c`・`exec/appmem*.c`・paging・pgalloc・physmem を連結し、全確保位置への失敗注入、IRQ 区間の状態、CR3 再ロードの回数、返却の順序を確かめる。
