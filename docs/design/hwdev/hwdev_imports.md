# sys/dev/hwdev/hwdev_imports.def — 設計
> 状態: **現行 (2026-10-31)** — 出所: HWDEV_CORE_API (D1 で移した)
ソース: `sys/dev/hwdev/hwdev_imports.def`

## 2. 核の関数の slot (26 関数、既存宣言と同型)

slot は 0 始まり、追加は末尾だけ。この表を `tools/hwdev_audit.py` が読み、GF 部品オブジェクトの未解決参照から内部参照と kernel imports を除いた集合と完全一致で照合する。型と呼出し元は宣言元・nm から JSON に生成する。

| slot | 関数 | 宣言元 |
|---|---|---|
| 0 | `trial_begin` | `sys/dev/video/geforce/diag/recover.h` |
| 1 | `trial_step` | `sys/dev/video/geforce/diag/recover.h` |
| 2 | `trial_end` | `sys/dev/video/geforce/diag/recover.h` |
| 3 | `recover_owner` | `sys/dev/video/geforce/diag/recover.h` |
| 4 | `recover_owner_configure` | `sys/dev/video/geforce/diag/recover.h` |
| 5 | `recover_safe_point` | `sys/dev/video/geforce/diag/recover.h` |
| 6 | `recover_plan_enter` | `sys/dev/video/geforce/diag/recover.h` |
| 7 | `recover_plan_leave` | `sys/dev/video/geforce/diag/recover.h` |
| 8 | `recover_plan_access` | `sys/dev/video/geforce/diag/recover.h` |
| 9 | `recover_plan_check` | `sys/dev/video/geforce/diag/recover.h` |
| 10 | `recover_bind_plan` | `sys/dev/video/geforce/diag/recover.h` |
| 11 | `recover_budget_apply` | `sys/dev/video/geforce/diag/recover.h` |
| 12 | `recover_bar_probe` | `sys/dev/video/geforce/diag/recover.h` |
| 13 | `recover_profile_crc` | `sys/dev/video/geforce/diag/recover.h` |
| 14 | `recover_access` | `sys/dev/video/geforce/diag/recover.h` |
| 15 | `recover_restore_point` | `sys/dev/video/geforce/diag/recover.h` |
| 16 | `recover_target_address` | `sys/dev/video/geforce/diag/recover.h` |
| 17 | `recover_window_hal_init` | `sys/dev/video/geforce/diag/recover.h` |
| 18 | `recover_window_access` | `sys/dev/video/geforce/diag/recover.h` |
| 19 | `recover_window_cleanup` | `sys/dev/video/geforce/diag/recover.h` |
| 20 | `recover_gf_pre` | `sys/dev/video/geforce/diag/recover.h` |
| 21 | `recover_gf_result` | `sys/dev/video/geforce/diag/recover.h` |
| 22 | `recover_gf_screen_safe` | `sys/dev/video/geforce/diag/recover.h` |
| 23 | `recover_hal_wait_frame` | `sys/dev/video/geforce/diag/recover.h` |
| 24 | `kpeek_call` | `sys/platform/pc98/kdebug/kpeek.h` |
| 36 | `recover_gf_runner_access` | `sys/dev/video/geforce/diag/recover.h` |

核の型付き表では core 関数だけを表の順に並べる (最後のポインタは 26 番目)。

- **G2**: `recover_hal_wait_frame` (recover.h・recover_profile.c) は gfinit.c の static const 初期化子で関数ポインタとして使う。アドレスは stub のアドレスで、R_386_32 再配置を受ける。

## 3. HWDEV の import の生成 (G3)

- 核は **26** 関数で上表に不足・余剰なし。`recover_hal_wait_frame` は直接 CALL が無くてもアドレス参照が必要。
- kernel imports は **22** (層 3 の診断取得口 `geforce_accel_ops` と再表示 `gfx_present`、層4のカーネル診断 `gf_display_test`、層5のカーネル診断 `gf5_test`、guard lifetime の資源照会と device map/unmap を末尾追加)。HWDEV 内の index は25から次表順に続ける。既存 kdebug import の番号は再利用/変更せず、宣言の取得元として照合する。

| HWDEV index | 関数 | 既存 kdebug index |
|---|---|---|
| 25 | kprintf | 0 |
| 26 | kmemset | 69 |
| 27 | pgalloc_alloc_phys | 109 |
| 28 | boot_image_info | 212 |
| 29 | pgalloc_free_n_owner | 214 |
| 30 | serial_get_setup | 245 |
| 31 | pci_cfg_read32 | 259 |
| 32 | pci_cfg_read16 | 260 |
| 33 | pci_cfg_write32 | 261 |
| 34 | pci_cfg_write16 | 262 |
| 35 | gf_production_selected | 307 |
| 37 | geforce_foundation_ops | 308 |
| 38 | serial_puts_polled | 120 |
| 39 | sys_time_now | 123 |
| 40 | geforce_accel_ops | 309 |
| 41 | gfx_present | 310 |
| 42 | gf_display_test | 311 |
| 43 | gf5_test | 312 |
| 44 | geforce_resource_lookup | 316 |
| 45 | paging_device_map | 317 |
| 46 | paging_device_unmap | 318 |
| 47 | pci_bar_get | 323 |

M5 の末尾 3 slot は GeForce production adapter 用に保持する共有宣言。HWDEV の診断オブジェクトには呼出しがないため、`tools/hwdev_audit.py` はこの 3 名だけを未使用でも保持し、それ以外の過不足は従来どおり拒否する。

`boot_image_info`・`pci_cfg_write32/16` も要る。`kmemcpy` は現行 GF の未解決参照に無いので入れない。`__os32_abi_1_mem_4_fmt_4` は既存 linker の世代契約で、import slot にしない。

- GF ソースは変更せず、既存関数名の生成 stub が HWDEV imports の slot 経由で核を呼ぶ。装置境界は recover.h の recover_ops/opaque を再利用し、local_cleanup/screen_safe/raw_config/read/write を重複定義しない。wrapper が既存 gfinit_call と gfinit_recover_end を call/release_work へ接続する。
