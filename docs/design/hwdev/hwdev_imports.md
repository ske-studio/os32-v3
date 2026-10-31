# hwdev/hwdev_imports.def — 設計
> 状態: **現行 (2026-10-31)** — 出所: HWDEV_CORE_API (D1 で移した)
ソース: `hwdev/hwdev_imports.def`

## 2. 核の関数の slot (26 関数、既存宣言と同型)

> [要確認] 現在の監査ツールは移設元の表を参照している。

slot は 0 始まり、追加は末尾だけ。この表を `tools/hwdev_audit.py` が読み、GF 部品オブジェクトの未解決参照から内部参照と kernel imports を除いた集合と完全一致で照合する。型と呼出し元は宣言元・nm から JSON に生成する。

| slot | 関数 | 宣言元 |
|---|---|---|
| 0 | `trial_begin` | `kdebug/recover.h` |
| 1 | `trial_step` | `kdebug/recover.h` |
| 2 | `trial_end` | `kdebug/recover.h` |
| 3 | `recover_owner` | `kdebug/recover.h` |
| 4 | `recover_owner_configure` | `kdebug/recover.h` |
| 5 | `recover_safe_point` | `kdebug/recover.h` |
| 6 | `recover_plan_enter` | `kdebug/recover.h` |
| 7 | `recover_plan_leave` | `kdebug/recover.h` |
| 8 | `recover_plan_access` | `kdebug/recover.h` |
| 9 | `recover_plan_check` | `kdebug/recover.h` |
| 10 | `recover_bind_plan` | `kdebug/recover.h` |
| 11 | `recover_budget_apply` | `kdebug/recover.h` |
| 12 | `recover_bar_probe` | `kdebug/recover.h` |
| 13 | `recover_profile_crc` | `kdebug/recover.h` |
| 14 | `recover_access` | `kdebug/recover.h` |
| 15 | `recover_restore_point` | `kdebug/recover.h` |
| 16 | `recover_target_address` | `kdebug/recover.h` |
| 17 | `recover_window_hal_init` | `kdebug/recover.h` |
| 18 | `recover_window_access` | `kdebug/recover.h` |
| 19 | `recover_window_cleanup` | `kdebug/recover.h` |
| 20 | `recover_gf_pre` | `kdebug/recover.h` |
| 21 | `recover_gf_result` | `kdebug/recover.h` |
| 22 | `recover_gf_screen_safe` | `kdebug/recover.h` |
| 23 | `recover_hal_wait_frame` | `kdebug/recover.h` |
| 24 | `kpeek_call` | `kdebug/kpeek.h` |
| 36 | `recover_gf_runner_access` | `kdebug/recover.h` |

核の型付き表では core 関数だけを表の順に並べる (最後のポインタは 26 番目)。

- **G2**: `recover_hal_wait_frame` (recover.h:373・recover_profile.c:119) は gfinit.c:80 の static const 初期化子で関数ポインタとして使う。アドレスは stub のアドレスで、R_386_32 再配置を受ける。

## 3. HWDEV の import の生成 (G3)

- 核は **26** 関数で上表に不足・余剰なし。`recover_hal_wait_frame` は直接 CALL が無くてもアドレス参照が必要。
- kernel imports は **11** (P4 の本番選択門 `gf_production_selected` を末尾追加)。HWDEV 内の index は25から次表順に続ける。既存 kdebug import の番号は再利用/変更せず、宣言の取得元として照合する。

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
| 35 | gf_production_selected | 308 |

`boot_image_info`・`pci_cfg_write32/16` も要る。`kmemcpy` は現行 GF の未解決参照に無いので入れない。`__os32_abi_1_mem_4_fmt_4` は既存 linker の世代契約で、import slot にしない。

- GF ソースは変更せず、既存関数名の生成 stub が HWDEV imports の slot 経由で核を呼ぶ。装置境界は recover.h:200–219 の recover_ops/opaque を再利用し、local_cleanup/screen_safe/raw_config/read/write を重複定義しない。wrapper が既存 gfinit_call と gfinit_recover_end を call/release_work へ接続する。
