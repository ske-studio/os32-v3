# kdebug/selftest_exec.c — 設計
> 状態: **現行 (2026-10-31)** — 出所: SLIM_KERNEL_DESIGN.md・SLIM_KERNEL_IMPORTS.md (D1 で移した)
ソース: `kdebug/selftest_exec.c`

## SLIM_KERNEL_DESIGN

```c
/* exec試験内: 状態を一度取得し、従来と同じ分岐・検査に使う */
struct kselftest_exec_view view;
kdi_selftest_exec_view(&view);
```

- exec の 2 本は `kdebug/selftest_exec.c` へ移し、各入口で snapshot を 1 回取る。

## SLIM_KERNEL_IMPORTS

| 記号 | 種類 | 呼出し名 |
| --- | --- | --- |
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
| `kdi_selftest_exec_view` | FUNC | `kdi_selftest_exec_view` |
| `kapi_reserved_nosys` | FUNC | `kapi_reserved_nosys` |
| `kmemset` | FUNC | `kmemset` |
| `os32x_layout_check` | FUNC | `os32x_layout_check` |
| `ring3_ls_start` | PTR | `kdi_ptr_ring3_ls_start` |
| `ring3_ls_end` | PTR | `kdi_ptr_ring3_ls_end` |
| `__kernel_resident_end` | PTR | `kdi_ptr___kernel_resident_end` |

- __kernel_resident_endはリンカ記号の番地が必要。GETTERでメモリ内容を読まない。既存生成形 `#define __kernel_resident_end (*kdi_ptr___kernel_resident_end())` と `&` の組合せを保持する。

- FUNCのstub番地と実関数番地は異なる（gen_kdebug_imports.py:254）。kapi_reserved_nosysとの等値比較には生のimport slot値を使う。

shlib_read_pageのcallback渡しにKDI_ADDRを使用。通常の関数呼出しは既存tail-jump stubのまま。
