# K6-RAM: 物理 RAM の人為的な上限 (16MB) の撤廃 — ホスト TDD 記録

票: `docs/archive/gui_v13/TASK_K6_ram_ceiling.md` (2026-09-11)。
ゲスト検証 (受入 M1〜M5) は PM / テスターの担当で、ここに記録があるのはホスト試験だけ。

## 実行した RED → GREEN

### 1. 物理番地の地図 (`test_physmem.py::test_high_ram_has_no_artificial_ceiling`)

- 「16MB 超を丸ごと RAM にし、15〜16MB と最上位だけを予約として抜く」を先行。
  RED: `MEM_SYSTEM_SPACE_BASE` / `MEM_HIGH_RAM_BASE` / `MEM_PHYS_MMIO_TOP` が無く
  コンパイル不能。`include/memmap.h` に 3 定数を足して GREEN。
- 128MB 相当 → `[16MB, 128MB)` が RAM、`[15MB, 16MB)` が RESERVED (RAM 0 ページ)。
- 4GB 相当 → RAM = `(15MB - 4MB) + (MEM_PHYS_MMIO_TOP - 16MB)`、最上位は MMIO。
  `physmem_legacy_end()` は高位 RAM を足しても 0xF00 のまま = exec の連続アリーナ不変。
- ハーネスの前文に `#include "memmap.h"` を追加 (既存の `pegc.h` / `wab_xe10.h` と同じ扱い)。

### 2. ページングの守備範囲 (`test_paging_bounds.py` / `paging_bounds_host.c`)

- 「paging_init が張るのは *検出量 x ブート窓* であって RAM の上限ではない」を先行。
  RED: `paging_boot_identity_end` が未宣言でコンパイル失敗。
  `PAGING_RAM_LIMIT` を撤廃し、クランプ先を `PAGING_BOOT_MAP_SIZE` へ、
  実際に張った上端を `paging_boot_identity_end()` で公開して GREEN。
- `paging_init(16384)` → `paging_boot_identity_end() == 4096`、16MB は present、
  ブート窓の途中 (16MB) 以降は Not-Present。従来の挙動と同一。

### 3. ブート結線 (`test_memory_boot.py::test_high_ram_above_16m`, 32768KB / 131072KB)

- RED: `FAIL limit == TEST_KB / (PAGE_SIZE / 1024)` — 池の上端が 15MB で止まる。
- `kernel/memory_boot.c` に高位 RAM の登録 (`physmem_exclude` x2 + `physmem_add_trusted`)
  と workspace の動的サイズ決定を入れて GREEN。検査内容:
  - `pgalloc_limit_pfn()` が検出量ちょうど (人為的な切り詰めが無い)。
  - `[0xF00, 0x1000)` が RESERVED で、`pgalloc_alloc_n_pfn` がそこから 1 ページも返さない (M5)。
  - `[MEM_PHYS_MMIO_TOP, 4GB)` が MMIO。
  - 検出量の**最終ページ**が確保でき、解放できる。
  - `sys_usable_mem_end()` は workspace 直下のまま = exec のレイアウト不変。
  - metadata / workspace が `[MEM_APP_BAND_MAX_TOP, MEM_SYSTEM_SPACE_BASE)` の内側。
  - ブート窓より上の PDE 8..N の PT が全部 workspace 由来。
- 途中で `test_huge_hint_no_promotion` (mem_kb = 0xFFFFFFFF) が RED に転落。
  原因は高位 RAM の量をローダ申告 `mem_kb` から導いていたこと。
  **検出器が確認した量だけ**を `boot_high_end` に置き、`memory_boot_init` はそれだけを見る
  設計に直して GREEN。ヒント単独では 1 ページも昇格しないことを
  `CHECK(!boot_high_end)` で常時検査する。

### 4. 表の動的確保 (`test_memory_boot.py::test_table_sizing_has_no_artificial_ceiling`)

- 32MB / 128MB は丸ごと通る (`memory_boot_high_fit` が入力を返す)。
- 4GB 相当 (`MEM_PHYS_MMIO_TOP / PAGE_SIZE`) では表の置き場所が尽きるので丸める。
  検査は「表は置き場所 (room) を超えない」「PDE 1 枚ぶん増やすと超える = 目一杯まで取る」
  「それでも 2GB 超を覆える」の 3 点で、具体的な数値定数は置かない。
- 置き場所が無い構成では 0 を返して legacy へ落ちる (でっち上げない)。

### 5. ブート順の固定 (`test_memory_boot.py::test_kernel_boot_order_and_failstop`)

- `memory_boot_detect` → `paging_init` → `memory_boot_init` の順であることを
  `kernel/kernel.c` のソース上で検査する行を追加。

## 実行結果 (2026-09-11)

| 試験 | 結果 |
|---|---|
| `tools/tests/test_physmem.py` | PASS (9) |
| `tools/tests/test_paging_bounds.py` | PASS (host ILP32 + i386-elf 目標コンパイル) |
| `tools/tests/test_pgalloc_model.py` | PASS (12) |
| `tools/tests/test_pgalloc_range.py` | PASS |
| `tools/tests/test_highram_stage.py` | PASS (9) |
| `tools/tests/test_memory_boot.py` | PASS (8) |
| `tools/tests/test_app_band_pde.py` | PASS |
| `tools/tests/test_device_reservation.py` | PASS |
| `tools/tests/test_multiapp_model.py` / `test_multiapp_impl.py` | PASS |
| `tools/tests/test_sbrk_tier.py` / `test_owner_reclaim.py` | PASS |
| `python3 tools/check_constraints.py` | EXIT=0 |
| i386-elf-gcc `-Wall -Wextra` (kernel.c / paging.c / pgalloc.c / physmem.c / memory_boot.c / sys.c / backend_cirrus.c) | 新規警告ゼロ |

`kernel/kernel.c` に残る 2 件 (`TVRAM_BPR` の再定義、`kprintf` の暗黙宣言) は
HEAD (`8d5ba3b`) でも同じく出る既存のもので、この票の変更とは無関係。

## ホスト試験で**証明していない**こと

- `memory_boot_detect()` の実ハード動作 (BIOS ワークエリア 0594h の読み、1MB ごとの
  書き込み確認、24bit ラップ判定)。ホストでは 0x594 も物理 0 も触れないので、
  ここは受入 M1/M2 (NP21/W) でしか確かめられない。
- 16MB 超を実際に張った後のアプリ起動 (M4) と 15MB 構成の回帰 (M3)。

---

## 6. K6-3: PEGC の probe が 15MB 構成で 9801 に落ちる回帰 (2026-09-12)

PM 実測: 15MB (`ExMemory 16`) + `gfxmode pegc` + リセットで `hal_test` が
`backend pc98 (planar 4bpp)`、`sys_top_reserved` = 0。BIOS の PEGC ビットは立っている。
原因は `gfx/backend_pegc.c` の probe 段 2 `sys_get_mem_kb() * 1024 > PEGC_LINEAR_BASE` —
K6-RAM で上端の定義が「RAM の上端 / 1024」になり、15MB 機でも 17408 を返すようになったため。

### RED → GREEN

- 新しい口: `kernel/pgalloc.c` の `pgalloc_range_has_ram(first, end)` (PFN 半開)。
  ブート時に凍結した物理地図 `device_boot_map` を既存の `physmem_count(... PHYSMEM_RAM ...)` で
  見るだけで、状態は変えない。未初期化・範囲異常は保守的に真 (窓を張らせない)。
  `eligible` ビットマップではなく地図を見るので、他所の永久予約で `eligible` が落ちた RAM も
  「RAM あり」のまま数える。
- `tools/tests/memory_boot_host.c` の `TEST_RAMKB` (実構成そのもの、17408 / 33792 / 8192) に
  `CHECK(!pgalloc_range_has_ram(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE, high))` と
  「旧条件 (上端 > 窓) は高位 RAM のある構成でだけ真になる」の対比を追加。
  **RED の確認**: `pgalloc_range_has_ram` を一時的に旧規則 (`limit_pfn > end - 1`) に差し替えると
  `FAIL !pgalloc_range_has_ram(...)` が **17408 と 33792 だけ**で出て、8192 は通る
  (= 回帰の形が一致)。差し戻して GREEN。
- `tools/tests/pgalloc_range_host.c` に `device_window_ram()`:
  8MiB → 窓に RAM 無し (真)、16MiB 丸ごと RAM の legacy モデル → 窓が RAM (偽)、
  部分一致 1 ページ (`[hi-1, hi)`) でも真、窓の外は無し、逆順範囲と未初期化は保守的に真。
- `gfx/backend_pegc.c` は probe 段 2 を
  `pgalloc_range_has_ram(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE, MEM_HIGH_RAM_BASE / PAGE_SIZE)` に置換。
  `backend_pegc.c` はホストへ持ち込めない (probe が `_out` / `_in` で実ポートを叩き、
  `boot_splash_native_host.c` が直リンクしているのは `gfx_core.c` だけ) ので、
  判定はカーネル側の純粋な問い合わせに切り出してそちらを検査した。

### 実行結果 (2026-09-12)

| 試験 | 結果 |
|---|---|
| `tools/tests/test_memory_boot.py` | PASS (9) — 旧規則では 2 件 RED |
| `tools/tests/test_pgalloc_range.py` | PASS (`device_window_ram` 追加) |
| `tools/tests/test_physmem.py` | PASS (9) |
| `tools/tests/test_pgalloc_model.py` | PASS (12) |
| `tools/tests/test_highram_stage.py` | PASS (9) |
| `tools/tests/test_device_reservation.py` | PASS (8) |
| `tools/tests/test_paging_bounds.py` / `test_app_band_pde.py` | PASS |
| `tools/tests/test_boot_splash_native.py` | PASS (2) |
| `python3 tools/check_constraints.py` | EXIT=0 |
| i386-elf-gcc `-Wall -Wextra` (`gfx/backend_pegc.c` / `kernel/pgalloc.c`) | 警告ゼロ |

### ホスト試験で**証明していない**こと

- 実機 (NP21/W) の PEGC 復帰そのもの。`hal_test` が `backend pegc` を出すか、
  `sys_top_reserved` が 300KB になるかは PM / テスターの受入でしか確かめられない。
- `make` は一度も回していない (コーダーの禁止事項)。目標コンパイルは変更した 2 ファイルの
  単体 `i386-elf-gcc -c` のみ。
- `gfx/backend_cirrus.c` の `cirrus_win_usable()` は**この票では直していない**。同じ型の
  誤判定なので、**高位 RAM がある構成ではリニア窓 0x1000000 が常に不可になる**
  (K6-RAM 決裁 (4)、Cirrus レーン再開時)。
