# kdebug/selftest_lease.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: SLIM_KERNEL_DESIGN.md・SLIM_KERNEL_IMPORTS.md (D1 で移した)
ソース: `kdebug/selftest_lease.c`

## SLIM_KERNEL_DESIGN

- leaseはtables_imageごと移す（公開PD物理番地からの走査）。lease_next_tokenは公開せず、既存副作用を保存。

```c
/* lease試験末尾: 従来の公開結果変数への書込みを保持 */
kdi_selftest_lease_result((u32)rc);
```

- `kdi_selftest_lease_result` は `exec/lease.c` に既存観測変数への 1 代入として置く。shlib callback は付表どおり KDI_ADDR を使い、tables_image と既存の判定・復元順序・token の副作用は変えない。

## SLIM_KERNEL_IMPORTS

| 記号 | 種類 | 呼出し名 |
| --- | --- | --- |
| `paging_kernel_pd_phys` | FUNC | `paging_kernel_pd_phys` |
| `paging_current_cr3` | FUNC | `paging_current_cr3` |
| `ledger_owner_new` | FUNC | `ledger_owner_new` |
| `ledger_owner_retire` | FUNC | `ledger_owner_retire` |
| `pgalloc_alloc_phys` | FUNC | `pgalloc_alloc_phys` |
| `pgalloc_free_n_owner` | FUNC | `pgalloc_free_n_owner` |
| `ledger_surface_create` | FUNC | `ledger_surface_create` |
| `ledger_surface_release` | FUNC | `ledger_surface_release` |
| `paging_addrspace_create_lease` | FUNC | `paging_addrspace_create_lease` |
| `paging_addrspace_destroy` | FUNC | `paging_addrspace_destroy` |
| `lease_acquire` | FUNC | `lease_acquire` |
| `lease_check` | FUNC | `lease_check` |
| `lease_release` | FUNC | `lease_release` |
| `lease_revoke_all` | FUNC | `lease_revoke_all` |
| `paging_as_audit` | FUNC | `paging_as_audit` |
| `paging_lease_pte` | FUNC | `paging_lease_pte` |
| `paging_load_cr3` | FUNC | `paging_load_cr3` |
| `shlib_read_page` | FUNC | `shlib_read_page` |
| `kdi_selftest_lease_result` | FUNC | `kdi_selftest_lease_result` |
| `ledger_surfaces` | PTR | `kdi_ptr_ledger_surfaces` |

kapi_reserved_nosysの比較にKDI_ADDRを使用。通常の関数呼出しは既存tail-jump stubのまま。

| 記号 | 元の種類・操作 | r2での扱い |
| --- | --- | --- |
| tables_image / irq_save / irq_restore / P2V等 | 移植補助 / ヘッダinline・マクロ | tables_imageは同じ移植.oへ、IRQ/番地マクロは既存ヘッダ。別importは作らない |
