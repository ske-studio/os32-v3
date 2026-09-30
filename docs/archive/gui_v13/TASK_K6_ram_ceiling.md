# K6-RAM: 物理 RAM の人為的な上限 (16MB) を撤廃する — 上限はアーキテクチャ (4GB) だけ

> 発行: PM (2026-09-11) / 状態: **受入完了 (2026-09-12)**
> レーン: K (C89、カーネル背骨 + ブートローダ) / 前提: ユーザー決裁 (2026-09-11)
> 「OS で制限する必要は基本的に無い。Win32 の上限くらいに」。K5b の受入 G3/G6 (4 本同時、5 本目拒否) がこれに依存する。
>
> **方針 (ユーザー、2026-09-11)**: 上限は 32bit x86 のアーキテクチャ (PAE なし = 物理 4GB) だけ。Win32 の
> 実効 ≈3.2GB は 4GB からデバイスの MMIO / PCI 窓 / BIOS ROM の分を引いた値で、OS が絞っていたのではない。
> OS32 も同じ: **人為的な clamp を持たず、検出した RAM の量に応じて表 (pgalloc の bitmap、identity の PT) を
> 動的に取る**。RAM にならない領域 (15〜16MB の PEGC 窓、4GB 最上位の BIOS ROM ミラー / PCI 機の MMIO) は
> 予約として除外する。実質の上限は機体 / NP21/W が積める量。
> 排他: `boot/loader_hdd.asm` `boot/loader_fat.asm` `kernel/physmem.{c,h}` `kernel/pgalloc.{c,h}` `kernel/paging.{c,h}`
> `kernel/memory_boot.c` `include/memmap.h` `tools/tests/` の該当ホスト試験。`exec/**` `kapi/**` は触らない (K5b の領分)。

## ゴール

**16MB 超の物理 RAM を検出して pgalloc の池に入れる。人為的な上限定数 (16MB / 32MB / 128MB) は持たない** —
上限は `PHYSMEM_MAX_PFN` (= 1M ページ = 4GB、既存) だけ。受入は 32MB と 128MB で行い、NP21/W が積める最大でも
起動することを確かめる。アプリ帯の仮想レイアウト (`app.ld` / `MEM_*`) は動かさない — 増えるのは per-app 物理
(K5b-K) の供給源だけ。

## 現状の事実 (2026-09-11、PM が静的確認)

| # | 事実 | 根拠 |
|---|---|---|
| R1 | ローダは 512KB 刻みの書き込みプローブで `mem_kb` を数え `kernel_main(mem_kb, …)` へ渡す。上限の扱いはローダ側で確認する (16MB で止めているなら 16MB 超は永遠に見えない) | `boot/loader_fat.asm:252-290`、`boot/loader_hdd.asm:286-289` |
| R2 | `physmem_bootstrap_legacy()` は `mem_kb` を **16MB で clamp** し、注記に「16MiB 超の報告は取り込まない」 | `kernel/physmem.c:94-95`、`kernel/physmem.h:62-69` (`PHYSMEM_LEGACY_MAX_PFN = 4096`) |
| R3 | `pgalloc` の bitmap は `LEGACY_WORDS = (PHYSMEM_LEGACY_MAX_PFN+31)/32` で 16MB 分。範囲・admit も同じ定数で弾く | `kernel/pgalloc.c:11,58,71,106` |
| R4 | `paging_init()` は `PAGING_RAM_LIMIT` (= 32MB、注記「守備範囲を 32MB に広げた」) で頭打ちにして identity を張る | `kernel/paging.c:141-158` |
| R5 | 15〜16MB (`0xF00000〜0xFFFFFF`) は 9821 の PEGC リニア窓 = **RAM として使ってはいけない穴** | `include/memmap.h:280-284` (`MEM_APP_BAND_DEVICE_FLOOR = 0x00F00000`)、`include/pegc.h` |
| R6 | K5a D5: 15MB 構成の 0xC00000 以上 ≈3MB が per-app 物理で初めて使える。上限を上げれば 16MB 以上が丸ごと供給源になる | `TASK_K5_multiapp.md` §設計 D5/D10 |
| R7 | 実測 (2026-09-11): 15MB・`heap_size = 0` の既定で GUI アプリは 3 本、4 本目が `Launch failed` | `TASK_K5B_gshell.md` 受入 G3 前半 |

## 作業

1. **検出**: PC-98 の BIOS ワークエリアで 16MB 超の容量を読む (`0x594` = 16MB 超の MB 数、`0x401` = 15MB までの 128KB 単位。
   正典は `docs/hw/` の UNDOCUMENTED、矛盾は UNDOCUMENTED を採る) か、ローダのプローブを 16MB 超へ伸ばす。
   どちらを採るかはソースと `docs/hw/` で根拠を出す。NP21/W の `ExMemory` の意味 (`~/np21w-src/src/mem.c` /
   `pccore.c`) も確認し、ini の値 → ゲストが見る MB 数の対応表を票に書く。
2. **physmem**: 16MB clamp を撤廃し、RAM の範囲を **[1MB, 15MB) + [16MB, N)** として登録 (15〜16MB は `PHYSMEM_RESERVED` /
   デバイス、4GB 最上位の ROM / MMIO も予約)。`PHYSMEM_LEGACY_MAX_PFN` は「旧ローダ互換の下限保証」の意味だけに
   縮め、**新しい上限定数は足さない** (上限は `PHYSMEM_MAX_PFN` のみ)。`physmem_bootstrap_legacy` / モデル経路
   (`memory_boot.c`) の両方。
3. **pgalloc**: bitmap を静的 16MB 分から**検出量に応じた動的確保**へ (4GB なら 128KB。RAM の先頭側から取る)。
   範囲検査は `PHYSMEM_MAX_PFN`。
4. **paging**: `PAGING_RAM_LIMIT` (32MB) を撤廃し、identity の PT を検出量ぶんだけ RAM から動的に取る
   (4GB なら 1024 枚 = 4MB — これも RAM から)。`paging_addrspace_create_n` が PDE をコピーする範囲を確認。
   カーネルの識別 PDE 0 (全 PD 共有) と、カーネル自身が使う低位 (0〜3MB) の前提を壊さない。
5. **memmap / 予約**: `sys_usable_mem_end()` / `sys_reserve_top()` (PEGC バックバッファ、メモリ末尾の 1MB 予約は
   ユーザー方針で可) が 16MB 超でも整合すること。`v86` のバッキング (`EXEC_DYN_RESERVE` の穴、低位メモリ) は不変。
6. **ホスト試験**: `test_physmem.py` / `test_pgalloc_model.py` / `test_paging_bounds.py` / `test_memory_boot.py` に
   「32MB / 128MB の入力で範囲が正しく登録され 15〜16MB が除外される」「上限超えは切り詰めではなく拒否/切り捨てを明示」を
   RED→GREEN で。記録は `tools/tests/k6_ram_tdd.md`。`build/sdk.mk` への登録は PM。

## 受入 (ゲスト、PM/テスター)

| ID | 試験 | 合格条件 |
|---|---|---|
| M1 | NP21/W `ExMemory` を 32MB 相当にして起動 | `ver`/kselftest 通過、`sys_mem_kb` (= `kernel.map` の番地) が 32MB 相当、pgalloc の空きページが増えている |
| M2 | 同 128MB、および NP21/W が積める最大 | 同上。人為的な上限に当たらない (切り捨てはデバイス予約だけ) |
| M3 | 15MB 構成の回帰 | regress 6 本、v86 -t、GUI アプリ 1 本 (K5b の G8 と同じ) が従来どおり |
| M4 | K5b の G3 | 32MB 以上で GUI アプリ 4 本が立ち、5 本目が `ERR_FULL` で拒否される (`heap_size = 0` の既定のまま) |
| M5 | 15〜16MB の穴 | 128MB でも `0xF00000〜0xFFFFFF` が RAM として配られない (pgalloc の範囲検査で証明) |

ini の変更 (`ExMemory`) は [D2]。PM がスキル `os32-emu-config` の手順で行い、値の許容表 (`np21w_ini_live.py` の
`EXMEMORY`) を 32 / 128 相当へ拡張する (別コミット)。

## この票に含めないもの

アプリ帯の仮想レイアウト変更、KAPI の追加、K5b の gshell 側。

---

## 実装メモ (コーダー、2026-09-11) — 決めたことと根拠

### A. 検出方式: BIOS ワークエリア **0594h** を採る (ローダのプローブは伸ばさない)

| 番地 | 型 | 意味 | 上限 |
|---|---|---|---|
| `0401h` | BYTE | `100000h`〜`FFFFFFh` の使用可能プロテクトモードメモリ、**128KB 単位** | `70h` (= 14MB)。「16MB システム空間を使用しない」設定のときだけ `78h` (= 15MB) |
| `0594h` | **WORD** | `1000000h` 以降の使用可能プロテクトモードメモリ、**MB 単位** | 機種依存 (PC-H98 / PC-9821Af 以降 / PC-9801BA2・BS2・BX2・BA3・BX3・BX4) |

正典は `docs/hw/undocumented/memsys.md` (0401h / 0594h の項)。**0594h はバイトではなく
ワード**で、NP21/W も `STOREINTELWORD` で 2 バイト書く (`np21w-src/src/bios/bios.c:241-248`)。

プローブを 16MB 超へ伸ばさなかった理由 (R1 の答え):

- ローダは `esi < 01000000h` で止まる (`boot/loader_fat.asm:276-278`、`loader_hdd.asm:285-287`)
  ので、16MB 超は現状まったく見えない。
- `F00000h`〜`FFFFFFh` は PC-98 の「16MB システム空間」で、**RAM ではない**:
  `F00000-F7FFFF` PEGC の 512KB リニア窓 / `F80000-F9FFFF` オープンバス /
  `FA0000-FFFFFF` は **`A0000h`〜`FFFFFh` のミラー** (テキスト・グラフィック VRAM と BIOS ROM)。
  書き込みプローブをここへ通すと VRAM を壊す。避けて数えるには結局ハードウェアの知識が要り、
  その知識はすでに BIOS ワークエリアに置いてある。
- I/O `043Bh` bit2 の「16MB 空間を通常メモリにする」設定は NP21/W が未実装 (読み書きのみのスタブ、
  `src/io/necio.c:15-23`) なので、エミュレータ上でこの穴が RAM になることは無い。
- ローダ (`.8086` の `boot_fat.asm`、PM 遷移がインラインの `loader_fat.asm`) に触らずに済む。

**申告は鵜呑みにしない。** `memory_boot_detect()` が 1MB ごとに 1 ダブルワードを書いて
(1) 24bit アドレスラップで低位を壊していないか (壊したら 1 語を復元して打ち切り)
(2) 読み戻しが一致するか
を見て、さらに 2 巡目で別名 (エイリアス) を弾く。**確認が通った連続分だけ**が
`PHYSMEM_SOURCE_MACHINE` として登録される。ローダの申告 `mem_kb` だけでは
高位 RAM は 1 ページも昇格しない (`test_memory_boot.py` が常時検査)。

### B. NP21/W の `ExMemory` → ゲストが見る RAM

ini キー `ExMemory` は **MB 単位**。既定 13、`Release|x64` (= `make build` の構成) は
`SUPPORT_LARGE_MEMORY` 付きで UINT16 / 最大 **4000** (`src/win9x/ini.cpp:476-480`,
`src/win9x/compiler.h:231-235`, `src/pccore.c:122,309-319`)。
DIPSW3-8 (`dipsw[2] & 0x80`) を立てると拡張メモリは丸ごと無効。

`CPU_EXTLIMIT16 = min(size + 100000h, F00000h)` (`src/i386c/ia32/ia32.c:155-157`) なので、
**低位 RAM は必ず `EFFFFFh` で終わる**。`ExMemory >= 15` では要求のうち 1MB がそのまま失われる。

| `ExMemory` | 16MB 未満の RAM | 16MB 以上の RAM | 使える拡張 RAM | `0401h` | `0594h` |
|---|---|---|---|---|---|
| 0 | `100000-10FFFF` (HMA のみ) | — | 64KB | 0 | 0 |
| 7 | `100000-7FFFFF` | — | 7MB | 56 | 0 |
| 13 (既定) | `100000-DFFFFF` | — | 13MB | 104 | 0 |
| 14 | `100000-EFFFFF` | — | 14MB | 112 | 0 |
| 15 | `100000-EFFFFF` | なし | 14MB (**1MB 損**) | 112 | 0 |
| 16 | `100000-EFFFFF` | `1000000-10FFFFF` | 15MB | 112 | 1 |
| **32** | 〃 | `1000000-20FFFFF` | 31MB | 112 | 17 |
| 63 | 〃 | `1000000-3FFFFFF` | 62MB | 112 | 48 |
| **128** | 〃 | `1000000-80FFFFF` | 127MB | 112 | 113 |
| 230 | 〃 | `1000000-E6FFFFF` | 229MB | 112 | 215 |
| 4000 (最大) | 〃 | `1000000-FA0FFFFF` | 3999MB | 112 | 3985 |

受入 M1 (32MB 相当) は `ExMemory = 32`、M2 (128MB) は `ExMemory = 128` (コーダー案)。
**PM 註 (2026-09-11)**: ini プリセット (`tools/np21w_ini_live.py`) は**ゲストが使える拡張メモリ量で命名**し、
`ram-32mb` = `ExMemory 33`、`ram-128mb` = `ExMemory 129` (16 以上は 16MB システム空間の 1MB が抜けるため +1)。
この場合 `sys_mem_kb` は 33 → `0x2200000 / 1024 = 34816`、129 → `0x8200000 / 1024 = 133120`。
OS32 が報告する `sys_mem_kb` は **RAM の上端アドレス / 1024** なので、
`ExMemory = 32` なら `0x2100000 / 1024 = 33792`、`= 128` なら `0x8100000 / 1024 = 132096`。
(`ExMemory` は 16MB を起点に数えるため 1MB ぶん上に出る。穴は `physmem` のモデル側に出る。)

### C. 表 (bitmap + 恒等 PT) の動的確保 — どこから取るか

置ける場所は **`[MEM_APP_BAND_MAX_TOP, MEM_SYSTEM_SPACE_BASE)` = `0xC00000`〜`0xEFFFFF` (3MB)**
に限られる。理由は 2 つとも既存の不変条件:

1. 下限 `MEM_APP_BAND_MAX_TOP`: 2 枚 PDE まで伸びたアプリが master のページテーブルを
   USER で恒等マップして任意物理を書けてしまう (`pgalloc.c` `init_model` の `ws_first` 検査)。
2. 上限 `MEM_SYSTEM_SPACE_BASE`: legacy アリーナ (`physmem_legacy_end`) の上端。
   その上は RAM ではない穴。

順序 (`memory_boot_init`):

1. `physmem_bootstrap_legacy()` で低位を組む → `top = physmem_legacy_end()` (15MB)。
2. 検出済みの高位 RAM を `memory_boot_high_fit()` で「表が置ける量」に丸める。
3. `[15MB,16MB)` を RESERVED、`[MEM_PHYS_MMIO_TOP, 4GB)` を MMIO、
   `[16MB, N)` を `PHYSMEM_SOURCE_MACHINE` の RAM として登録。**どれか 1 つでも失敗したら
   モデル不変で fail-stop** (静かに切り詰めない)。
4. `pgalloc_metadata_bytes()` が bitmap 2 面のページ数を、
   `memory_boot_workspace_pages()` が恒等 PT の枚数 + 予備 1 枚を返す。
5. 配置は上から `metadata = [top-md, top)`、`workspace = [top-md-ws, top-md)`。
   どちらも `physmem_reserve_ram` で一般確保から永久に外れる。
6. `pgalloc_stage_online()` が `paging_boot_identity_end()` を境に、
   **下は「すでに恒等である」ことを検証するだけ** (ブート時の保護属性を上書きしない)、
   **上は `paging_map_phys()` で今から張る** (PT は workspace から取る)。

規模: 32MB → 表 2 ページ / 128MB → 27 ページ (bitmap 2 + PT 24 + 予備 1) /
約 2.8GB で 3MB の帯を使い切る。

### D. 決裁事項 (PM / ユーザー判断)

**ユーザー決裁 (2026-09-12、PM 推奨どおり)**: (1) 2.8GB の頭打ちは**見送り** (実機 9821 は 128MB、NP21/W は最大 1024MB で実害なし)。(2) `sys_mem_kb` の定義は「RAM の上端 / 1024」のまま、`mem` の表示に実 RAM 合計を**並記**する (小票 — **v46 で `sys_ram_kb` 追加**、K6C に相乗り)。(3) 0594h を書かない機種は何もしない (15MB 止まりと文書化済み)。(4) Cirrus 線形窓は `WAB_XE10_LINEARWIN_SEL` を RAM の上 (0x20 = 512MB 等) へ動かす — **Cirrus レーン再開時の小票** (ini 切替 [D2] を伴う)。

1. **約 2.8GB で頭打ちになる。** C の置き場所 (3MB) が表で埋まるため。人為的な定数ではないが
   「4GB まで」を厳密に満たしたい場合は、恒等 PT を高位 RAM 自身から段階的に取る
   (4MB 張る → そこを PT 置き場にする、の梯子) 改修が要る。`reserve_table()` /
   `pgalloc_alloc_pt()` の信頼境界に手を入れる話なので、この票の範囲では見送った。
   NP21/W の GUI プリセット最大は 1024MB、実機 9821 は 128MB なので実害は無い。
2. **`sys_mem_kb` の意味を「RAM の上端アドレス / 1024」に確定した** (穴を含む)。
   受入 M1/M2 で読む値は B 表の右端の計算どおりになる。別の定義 (実 RAM の合計) を
   採るなら `kernel.map` を読む側と揃える必要がある。
3. **`0594h` を書かない機種では 16MB 超が見えない。** ワークエリアを持たない古い機種
   (PC-9801 の初期型など) は従来どおり 15MB 止まり。プローブを伸ばす代替案は A のとおり
   VRAM 破壊の危険があるので採らなかった。
4. **Cirrus (WAB Xe10) のリニア窓と高位 RAM は同じ `01000000h` を奪い合う。**
   `cirrus_win_usable()` が「実 RAM がそこまで届いていたら窓を開かない」で既に守っているが、
   4GB 構成で `sys_get_mem_kb() * 1024` が桁あふれして判定が裏返るので KB のまま比べるよう直した。
   結果として **16MB 超を積むと Cirrus バックエンドは窓を開けず、PEGC へ落ちる**
   (PEGC のリニア窓は `F00000h` = システム空間の中なので高位 RAM と無関係、常に使える)。
   受入 M1/M2/M4 を 9821 + Cirrus 構成で回すと「32MB にしたら Cirrus が消えた」に見えるので注意。
   本筋の解は窓を RAM の上へ動かすこと (`WAB_XE10_LINEARWIN_SEL` は `dat << 24` の `dat`
   なので `0x20` = 512MB 等を選べる) だが、gfx レーンの話なのでこの票では触っていない。

## 実機受入の記録 (PM / テスター)

| 受入 | 構成 | obs | 判定 |
|---|---|---|---|
| 註 | — | `np21w_ini_live.py ram-32mb --live-apply` は初回「unsupported or mismatched explicit process/config identity」で拒否 (os32-cycle が ini 引数なしで起動していたため)。ini を引数に付けて起動し直してから適用。適用後に「Windows executor cleanup timeout」と出たが ini は書き換わり再起動も成功 (ExMemory=33 を読み戻して確認) | — |
| ゲート | `f6ec520` | `make clean/all/external/check` exit=0 (テスター 22:48〜22:50) | 合格 |
| 配備 | 15MB (`ExMemory 16`) | NHD バックアップ後 `os32-cycle deploy` exit=0、`vmkernel.lz4` 454,203 B 一致、`ver` Build 22:49 / API v45 | 合格 |
| **M3** 15MB 回帰 | 同 | kselftest 44 / 0、regress 6 本 obs 全通過 (klibc 49/0、alloc_demo、ring3_fault kill → ver、パイプ、screenshot)、`v86 -t` result OK、`gui_demo` 1 本起動 (Widgets / Help 描画) → CUI 復帰、`fault_generation` 0。`sys_mem_kb` = 17408 (= 0x1100000 / 1024、新定義どおり) | **合格** |
| **M1** 32MB | `ram-32mb` (ExMemory 33、ユーザー承認 [D2]) | `mem` = 34816 KB、`sys_mem_kb` 34816、kselftest 44 / 0、`v86 -t` OK、`limit_pfn` 8704 (= 0x2200000 / 4KB、15MB では 15MB 止まりだった)、`total_pages` 7421 / `used_pages` 3344 (4 本起動時) | **合格** |
| **M4** G3 | 同 | GUI アプリ 4 本が立ち、5 本目は「Too many programs (4 max)」で拒否 (TASK_K5B_gshell.md G3) | **合格** |
| **M2** 128MB | `ram-128mb` (ExMemory 129) | `mem` = 133120 KB、`sys_mem_kb` 133120、kselftest 44 / 0、`v86 -t` OK、`limit_pfn` 33280 (= 0x8200000 / 4KB)、`total_pages` 31971 / `used_pages` 256 (起動直後)、`gui_demo` 1 本起動 (`used_pages` 1028) → CUI 復帰。人為的な上限には当たらない | **合格** |
| **M5** 15〜16MB の穴 | 128MB | `eligible` ビットマップ (0x00EFD000) を直接読み: PFN 0xF00〜0xFFF は **0 / 256**、0xE00〜0xEFF は 227 / 256 (上端の予約分だけ欠ける)、0x1000〜 と 0x8000〜 は 256 / 256。`used` も 0xF00〜0xFFF は 0 / 256 | **合格** |
| K7 後の再確認 | `8cc13d8` 配備 (vmkernel 454,351 B) | 8MB: kselftest 44 / 0、`gui_demo` 1 本が立つ (G6 合格、段 2)。32MB: 4 本起動 (W-3 解消確認)、regress 6 本 obs 全通過 | 合格 |
| 8MB 回帰 (K7 前) | `ram-8mb` (ExMemory 7) | `mem` = 8192 KB、kselftest 44 / 0、legacy 経路 (`limit_pfn 2048 / total_pages 1024 / used 256`)。**GUI アプリは立たない (G6 不合格 → K7)** | CUI は合格 |

## K6-3. PEGC の probe が 15MB 構成で 9801 に落ちる回帰 (2026-09-12、コーダー修正済み)

- `gfx/backend_pegc.c` の probe 段 2 が `sys_get_mem_kb() * 1024 > PEGC_LINEAR_BASE` で窓の可否を決めていた。K6-RAM 決裁 (2) で上端の定義が「RAM の上端 / 1024」になったため、15MB 構成 (`ExMemory 16`) でも 17408 になり、**穴** (`[0xF00000, 0x1000000)` = RAM にしない 15-16MB システム空間) が空いているのに 9801 プレーナへ落ちていた (`hal_test` が `backend pc98 (planar 4bpp)`、`sys_top_reserved` = 0)。
- 修正: `kernel/pgalloc.c` に `pgalloc_range_has_ram(first, end)` (PFN 半開、ブート時の物理地図を `physmem_count` で見る問い合わせ) を足し、probe 段 2 を `pgalloc_range_has_ram(MEM_SYSTEM_SPACE_BASE / PAGE_SIZE, MEM_HIGH_RAM_BASE / PAGE_SIZE)` に置換。8MB (legacy 経路) は従来どおり通る。ホスト TDD は `tools/tests/test_memory_boot.py` (17408 / 33792 / 8192) と `test_pgalloc_range.py` の `device_window_ram` で RED→GREEN 済み、実機は未検証。
- 決裁 (4) の `cirrus_win_usable()` は同じ誤判定の型 (上端で窓を決める) を残したまま = **高位 RAM がある構成ではリニア窓 0x1000000 が常に不可**。この票では触らず、Cirrus レーン再開時に同じ口 (`pgalloc_range_has_ram`) で直す。

**K6-3 の実機確認 (2026-09-12)**: 15MB 構成で `gfxmode pegc` → GUI が 480 ラインで上がり、全画面 GFX → 復帰 (T8 F1) も通る。Cirrus 側の同型の判定 (`cirrus_win_usable`) は決裁 (4) どおり Cirrus レーン再開時に直す。
