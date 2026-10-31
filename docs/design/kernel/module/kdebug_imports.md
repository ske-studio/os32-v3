# kernel/module/kdebug_imports.def — 設計
> 状態: **現行 (2026-10-31)** — 出所: SLIM_KERNEL_DESIGN・SLIM_KERNEL_IMPORTS・HWDEV_CORE_API (D1 で移した)
ソース: `kernel/module/kdebug_imports.def`

- 正典は `kernel/module/kdebug_imports.def`。生成JSONは編集しない（`tools/gen_kdebug_imports.py:2`）。

- FUNCは公開関数をカーネルで実行。元TUの私的関数やインライン化された公開関数の本体をkdebugへコピーしない。
- GETTER/SETTERは値、PTRは番地。データの記号欄は元symbol、名前欄は生成する呼出し名。
- 生成SETTER/GETTER/PTRは既存公開データの接続であり、新しい状態操作API「3口」とは別勘定（容量には算入）。

現在は264〜270 (push群/stat/read/owner_exit) と303 (drop_count SETTER) を retained とし、
番号・型・実体を維持する [ABI]。enable/disable は実 TVRAM/cursor の描画試験が使う。

E08 の時計互換口 127/128/174/175/181/182 は段1から `retained`。
段2では保存領域・get/set/clear/reset を保持し、時計からの読出し・増分を廃止した。
9 (resume mark) と80 (launch) の自己試験は状態を変更せず0を返す。
34 (DB)・60 (IRQ reset)・172/173 (IRQ quiet) はホスト試験が使うため実処理を保持する。
私有ABI印は段1で更新済みで、段2は同じ番号・型・retained指定から再生成する。
kernel/kdebug は対で再ビルドし、段1以前の契約はABI照合、段2以前の像はPAIR照合で拒否する。
公開KAPI slot224・SDKと製品のクランプ観測口147は変更しない。

| 記号 | 種類 | 呼出し名 |
|---|---|---|
| `appslot_cur` | FUNC | `appslot_cur` |
| `appslot_gfx_owner` | FUNC | `appslot_gfx_owner` |
| `con_sink_reader_get` | FUNC | `con_sink_reader_get` |
| `kbd_inject` | FUNC | `kbd_inject` |
| `kbd_inject_pending` | FUNC | `kbd_inject_pending` |
| `kbd_inject_take` | FUNC | `kbd_inject_take` |
| `kbd_inject_peek` | FUNC | `kbd_inject_peek` |
| `kbd_inject_discard` | FUNC | `kbd_inject_discard` |
| `kdi_selftest_inj_push` | FUNC | `kdi_selftest_inj_push` |
| `kbd_inject_drop_count` | GETTER | `kdi_get_kbd_inject_drop_count` |
| `kbd_inject_drop_count` | SETTER | `kdi_set_kbd_inject_drop_count` |

| 記号 | 種類 | 呼出し名 |
|---|---|---|
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

| 記号 | 種類 | 呼出し名 |
|---|---|---|
| `kdi_selftest_exec_view` | FUNC | `kdi_selftest_exec_view` |
| `paging_pte_flags` | FUNC | `paging_pte_flags` |
| `vfs_cwd_user` | FUNC | `vfs_cwd_user` |
| `vfs_cwd` | FUNC | `vfs_cwd` |
| `vfs_devname_user` | FUNC | `vfs_devname_user` |
| `vfs_devname` | FUNC | `vfs_devname` |
| `kstrcmp` | FUNC | `kstrcmp` |
| `path_get_drive_user` | FUNC | `path_get_drive_user` |
| `path_get_drive` | FUNC | `path_get_drive` |
| `path_get_cwd_user` | FUNC | `path_get_cwd_user` |
| `path_get_cwd` | FUNC | `path_get_cwd` |
| `ring3_wm_enter` | FUNC | `ring3_wm_enter` |
| `ring3_wm_leave` | FUNC | `ring3_wm_leave` |
| `ring3_in_syscall` | GETTER | `kdi_get_ring3_in_syscall` |
| `ring3_in_syscall` | SETTER | `kdi_set_ring3_in_syscall` |

| 記号 | 種類 | 呼出し名 |
|---|---|---|
| `kapi_reserved_nosys` | FUNC | `kapi_reserved_nosys` |
| `kmemset` | FUNC | `kmemset` |
| `os32x_layout_check` | FUNC | `os32x_layout_check` |
| `ring3_ls_start` | PTR | `kdi_ptr_ring3_ls_start` |
| `ring3_ls_end` | PTR | `kdi_ptr_ring3_ls_end` |
| `__kernel_resident_end` | PTR | `kdi_ptr___kernel_resident_end` |

## 11. M3c の旧 import 保持と像の境界

| kdebug slot | symbol | 扱い |
|---:|---|---|
| 264 | pci_cfg_read32 | retained |
| 265 | pci_cfg_read16 | retained |
| 266 | pci_cfg_write32 | retained |
| 267 | pci_cfg_write16 | retained |
| 308 | gf_production_selected | retained (P4、HWDEV の診断入口だけが呼ぶ) |

GF だけが使っていた上の口は、既存 201 と同じ `.def` の `retained` で保持する。

> [要確認] 原文の PCI retained slot 264〜267 は現行 .def では 259〜262。264〜270 は con_sink push/stat/read/owner_exit、303 は con_sink_drop_count、308 は gf_production_selected。

順序・型・実装を維持し、private 生成契約のみ再生成する (KAPI と ops の版は不変)。

> [要確認] 現行 kernel/module/kdebug_imports.def は index 0〜308 の 309 枠。原文の旧件数は写さず保持契約だけを切り出した。
