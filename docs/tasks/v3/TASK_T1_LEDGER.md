# TASK_T1_LEDGER — T1: 物理地図と所有権台帳 (設計票)

> 状態: **設計中 (2026-09-30)** — P1 メモリマップ再構築の 2 番目の票 T1 の設計票。ユーザーが T1 の着手を承認 (2026-09-30)。範囲は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §6 の **T1 の行**で、決定 D1〜D36 は再議論しない。段の分割は T1a〜T1f (§4)。Codex との突き合わせ (§6 の X9〜X16) は未実施、コードは未着手。
>
> 発行: コーダー `claude-opus-5-5` (2026-09-30)、PM の指示による。基点 main `c9a8973` (T0 = C11 は受入完了、[C1] は gnu11)。
> 正典の関係: 決定・帯・規則の正典は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) (§0 D1〜D36、§2 帯と lease、§3 台帳と P2V/V2P、§3-5 R1〜R7、§4-5 起動順、§4-7 ローダ契約、§6 T1 の行、§7 受入、§8-4 X4、§10 U1〜U24)。番地の正典は `include/memmap.h`、地図は [`../../02_memory.md`](../../02_memory.md) §2-1 (生成)。この票は **T1 の実装範囲・現状・設計・段・受入**だけを持つ。
> 入力: [U6_PENDING_REVIEW](U6_PENDING_REVIEW.md) §1-4・§1-5、[DEVICE_RESERVATION](../settings/DEVICE_RESERVATION.md) (D33: 核を T1 の台帳へ)、[MEMORY_RAM_INTEGRATION](../../archive/settings/MEMORY_RAM_INTEGRATION.md) (D32: 受入 2 点を T1 へ、撤回済み)、[V3_PLAN_DRAFT](V3_PLAN_DRAFT.md) §3 P1・P4、[APP_BAND_PDE](../memory/APP_BAND_PDE.md)。
> 本文の `file:line` は基点 `c9a8973` の行。

---

## 1. 範囲

### 1-1. T1 に入るもの (TASK_MEMMAP_V3 §6 T1 の行を漏れなく)

| # | 項目 | 出所 | 段 |
|---|---|---|---|
| 1 | 物理地図 + 所有権台帳 (RAM の存在 / 割当可否 / owner / 写像を別の情報に、PFN 半開区間) | D2、§3-1 | T1a |
| 2 | **SURFACE / lease の型** (記述子の型と台帳の表まで。lease の付け外し API は T2) | §2-2、X3 | T1e |
| 3 | 池を **model 経路で全 RAM 量に** — legacy `pgalloc_init` の fallback を撤去 (8MB を含む) | §2-1、D32 | T1b |
| 4 | `MEM_POOL_BASE` ほか物理側の定数の分離 (`MEM_PHYS_RAM_CEILING` = 0x80000000 を含む) | D4、D11、§2-3 ⑦ の「物理 (池)」行 | T1b |
| 5 | owner タグ — **AS owner / 永続 owner の 2 種** (R5)、モジュール owner (R7)、device owner (X4) | R5、R7、X4 | T1a |
| 6 | `dma_alloc(size, align, limit)` と DMA プール内の最悪の並び (R4) | D5、R4 | T1c |
| 7 | **集積域・同梱域の予約規則** (定数と台帳初期化の規則。ローダ側の実体は T5b / T6b) | §4-5 | T1b |
| 8 | `sys_reserve_top` の撤去 | D3 | T1e |
| 9 | BB のブート時確保 (owner=boot) と gshell への移譲。**量は識別で決めた backend の量を probe の前に** | §2-2、X7-1 | T1e |
| 10 | **P2V / V2P の導入と物理ポインタ直接参照の監査 + 検査** | D20、§3-4、X7-3 | T1a (台帳の呼び手) + T1f (残り全部と検査) |
| 11 | 割り込み中の確保・解放を**数える**検査 (R1。panic への切り替えは T2) | R1 | T1a |
| 12 | モジュール owner の一括回収 (R7。実モジュールは T4 以降なので API と合成試験まで) | R7 | T1a |
| 13 | DEVICE_RESERVATION の核 (`pgalloc_device_reserve` / `sys_device_reserve_core`) を**台帳の MMIO 登録**に載せ直す (複数 span の一括 commit・永久保持・owner、`test_device_reservation.py` 流用) | D33 | T1d |
| 14 | **MMIO 登録の形 (X4)**: 検証済み資源レコード、予約 / 写像 / 面の範囲の分離、全 span 検査後の一括 commit、永久予約とモジュール回収の分離 | X4、§8-4 | T1d |
| 15 | Cirrus の写像 (`paging_map_phys`) を probe の前へ (識別 → 予約 → 写像 → probe) | D33 | T1e |
| 16 | 受入 (D32): 8MB / 17MB / 64MB のすべてでモデル経路、高位 RAM の登録源は `memory_boot_detect` だけ (U24 の資料照合を含む) | D32、U24 | T1b |

### 1-2. T1 に入れないもの (境界)

| 何 | どこ | T1 との境界 |
|---|---|---|
| アプリ帯 0x80000000 / lease 窓 0xF0000000、共有 USER 写像 (`exec.c:1932-1961`) と共有 PT 経路 (`paging.c:796`) の撤去、lease の付け外し API・検査 3 段・受入 (d) | **T2** | T1 は SURFACE の**型と表**と、BB・窓を SURFACE として登録するところまで。`lease_count` は T1 の間 0 のまま (解放時の検査にだけ使う) |
| `exec_child_claim` / `EXEC_DYN_RESERVE` / `MEM_APP_BAND_DEVICE_FLOOR` / `--cpl0` / `ring3_band_ram_top` の撤去 | **T2** (§6 T2 の行「claim / DEVICE_FLOOR / `--cpl0` 撤去」) | T1 は `sys_reserve_top` だけを消す。CPL=0 の子のための identity アリーナの上端は T1 の間だけ台帳から求める (§3-6) |
| 強制脱出を移譲 / 回収の 2 段に割る (R1 の実装)、R1 の検査を panic に | **T2** | T1 は数えて報告するだけ |
| `mem_map` / `mem_unmap`、trim 通知 (R2、D23〜D25) | **T2** | T1 の台帳 API は AS owner を明示で受ける形にしておく (§3-2) |
| カーネル帯 3MB 化・KERNEL_SLACK の池への登録・DMA プールを 0x3E0000 (64KB 整列) へ・固定 PT を画像の外へ | **T3** | T1 の DMA プールは今の 0x2E8000 のまま (64KB 境界をまたぐので最大 32KB、`dma_pool.h:31-35`)。`dma_alloc` の口と検査だけ T1 |
| 集積域 0x500000〜 / 同梱域 0x600000〜 の**ローダ側**・`paging_init` / 台帳をルートマウントの前へ (U19) | **T5b / T6b** | T1 は定数と台帳初期化の規則 (bootinfo が同梱を申告したら owner=bundle で予約、集積域は池へ) とホスト試験まで。起動順はいまのまま (ルートマウントが `paging_init` より前、`kernel.c:434` < `:505`) |
| 実モジュールのローダ (§4-7 の検証・公開・IRQ 結び付け) | **T4 / T5a / T5c** | T1 は owner=モジュール名 の一括回収と bundle → モジュールの移譲の API と合成試験 |
| 破壊的な probe / enable を GUI 境界へ、識別 / probe の分離 (gfx バックエンド 2 本 + glue)、Trident の実測 BAR、GUI 境界での同一性の再確認 | **P4** (T2 の後) | T1 は起動時の「副作用のない識別 → 予約 → 写像」までと、資源レコードの型・登録 API。probe は今の `gfx_prepare_backend` (`kernel.c:707`) の位置のまま |
| planar BB (0x6A000) を池へ | **T7a** | T1 は planar BB を**固定の SURFACE** (owner=boot → gshell) として台帳に登録するだけ |
| 余りの確定値 (`pgalloc_free_pages` の実測で §4-3 を更新) | **T3** | T1 は台帳の実サイズ (U1 のうち owner/lease 台帳の分) を測って報告する |

---

## 2. 現状の調査 (基点 c9a8973)

### 2-1. 物理メモリを誰がどこで確保・予約・写像しているか

「IRQ」の列は割り込みフレーム (IRQ ハンドラ本体か、その上で走る `ring3_abort_check` / 例外ハンドラ) から到達するか。

| 何 | どこ (file:line) | 方式 | owner (今) | 寿命 | IRQ |
|---|---|---|---|---|---|
| 物理地図 (起動時) | `kernel/memory_boot.c:184-225` (`memory_boot_init`)、`kernel/physmem.c` | `struct physmem` (最大 64 区間、RAM / RESERVED / MMIO / UNKNOWN) を作り、`pgalloc` に凍結して渡す | — | 起動時 1 回 | 無 |
| 高位 RAM の検出 | `kernel/memory_boot.c:73-113` (`memory_boot_detect`) | BDA 0594h + 1MB ごとの書き込み検証 + 2 巡目の別名検査 → `physmem_add_trusted(MACHINE)` (`:163`) | — | `paging_init` の前、IF=0 | 無 |
| **legacy fallback** | `kernel/memory_boot.c:202-215` | metadata + workspace を `MEM_APP_BAND_MAX_TOP` (0xC00000) より上に置けない構成は `pgalloc_init(mem_kb)` へ。**8MB (と 12MB 前後) は今もここ** — 高位 RAM は 1 ページも登録されない | — | 起動時 | 無 |
| 割当可否 + 割当済み | `kernel/pgalloc.c:17-54` | eligible と allocated の 2 bitmap。model 経路は metadata を低位 RAM の末尾に動的配置、legacy は BSS の `legacy_metadata` (`:16`、1KB) | **無し** (bit だけ) | kernel 寿命 | — |
| 汎用の確保 | `pgalloc.c:248-296` (`alloc_n_pfn` 最初適合、`pgalloc_alloc_n` / `_page` / `_n_range`) | `[PGALLOC_BASE 0x400000, generic_end)` を線形探索、IRQ 保存区間 | 無し | 呼び手次第 | 解放は有 (下) |
| 永久予約 | `pgalloc.c:325-348` (`pgalloc_reserve_pfn`) | eligible を落とす。解除 API 無し | 無し | 永久 | 無 |
| 固定番地の claim | `pgalloc.c:351-368` (`pgalloc_mark_used`) | 空きだけ allocated に。既割当は冪等 (参照数なし) | 無し | 呼び手が free | 無 |
| device broker の核 | `pgalloc.c:369-471` (`pgalloc_device_reserve`)、`kernel/sys.c:16-31` (`sys_device_reserve_core`) | 最大 16 span、owner (kernel 寿命 ID)、正規化 → 全検査 → 一括 commit、同 owner の完全一致だけ冪等。**呼び手は試験だけ** (`include/sys.h:14`「integration is pending」) | device owner | 永久 | 無 |
| PT workspace | `pgalloc.c:161-190` (`pgalloc_alloc_pt` / `_free_pt`) | model 経路の専用 bitmap `workspace_used` (BSS、16MB 分) | 無し | kernel | 無 |
| PT (master の動的) | `kernel/paging.c:394-421` (`reserve_table`) | model は workspace、legacy は `MEM_APP_BAND_MAX_TOP` より上の恒等 RW ページを 1 枚ずつ | 無し | kernel | 無 |
| AS の PD / PT | `paging.c:706-712` (作成)、`:770-773`・`:925` (解放) | `pgalloc_alloc_page` × (1 + 帯の PDE 数) | 無し (AS 構造体が保持) | AS | **有** (CTRL+STOP・#PF の畳み) |
| アプリの私有ページ | `exec/exec.c:1083-1110` (`app_map_region`)、`:1919-1922` | 連続を試し、だめならページ単位。回収は PTE を辿る (`paging_addrspace_free_user_range`) | 無し (PTE が暗黙の owner) | AS | **有** |
| CPL=0 子の identity アリーナ | `exec.c:939-1000` (`exec_child_claim` / `exec_cpl0_claim` / `_release`) | `[MEM_EXEC_LOAD_ADDR, sys_usable_mem_end())` を A / B に分け `pgalloc_mark_used`、間の `EXEC_DYN_RESERVE` は空ける | 無し | CPL=0 の子が居る間 | 無 |
| shlib 帯 | `kernel/shlib.c:95` (`pgalloc_mark_used(MEM_SHLIB_BASE, 256 ページ)`)、失敗経路で `pgalloc_free_n` (`:101`〜`:221`、13 か所) | 固定番地 claim | 無し | kernel (失敗時は返す) | 無 |
| shlib data 複製 | `shlib.c:278` (確保)、`:311` (解放) | `pgalloc_alloc_n(data_pages)` → `kmemcpy((void *)phys, …)` (`:285`) | 無し (attach 表) | AS | **有** (畳みの中の detach) |
| V86 バッキング | `kernel/v86_mem.c:44` (確保 160 ページ連続)、`:165` (解放)。teardown は `v86.c:701,820,878,888,894,922` | `pgalloc_alloc_n(160)` = 640KB **連続** | 無し | V86 セッション | 要確認 (V86 の脱出は IRQ0/IRQ1 のスタブから `v86_exit_to_kernel` で longjmp、`isr_stub.asm:480-483,521-524`) |
| PEGC BB | `gfx/backend_pegc.c:690-705` (`pegc_reserve_backbuffer`) | `sys_reserve_top(300KB)` (= `pgalloc_reserve_pfn` + `sys_top_reserved`) → 念のため `pgalloc_mark_used` (永久予約済みなので実際は何もしない) → `kmemset((u8 *)s_bb_phys, …)` | 無し (`s_bb_phys`) | kernel | 無 |
| `sys_reserve_top` | `kernel/sys.c:185-214` | 使用可能上限の直下を切り出し、上限 (`sys_frozen_exec` / `sys_top_reserved`) を下げる。唯一の利用者は PEGC BB | 無し | 永久 | 無 |
| planar BB | `include/memmap.h:148-149` (0x6A000、128KB)、`gfx/gfx_core.c:17-20`、`gfx/backend_pc98.c:154` | 低位の固定番地 | 無し | 永久 | 無 |
| 低位の固定用途 | `memmap.h:129-149` (フォントキャッシュ 0x1000、Unicode 表 0x4A000、BB 0x6A000、bootinfo 0x7E00)、mailbox 0x90000 | 固定番地。model では `[0, 0x400000)` ごと RESERVED (`physmem.c` の legacy bootstrap) | 無し | 永久 | 無 |
| DMA プール | `memmap.h:288-290` (0x2E8000、64KB)、`kernel/dma_pool.c`、写像は `paging.c:303-304` | 16 ページの bitmap + span 表 (静的)。**0x2F0000 の 64KB 境界をまたぐので 1 回の最大は 32KB** (`dma_pool.h:31-35`)。顧客は PCM リングだけ (`drivers/pcm_cs4231.c:525`)。**82557 のドライバはまだツリーに無い** (PCI 表と文書だけ) | span 表 | 呼び手が free | **有** (設計上 IRQ から呼べる、`dma_pool.c:3-5`。PCM の畳みは CTRL+STOP の IRQ フレームの中、R1) |
| FDC の DMA バッファ | `drivers/fdc.c:54` (BSS の `s_fdbuf`)、`:63,748,851,970` で `(u32)` → 8237 | カーネル BSS を物理として装置へ | — | kernel | 有 (FDC IRQ) |
| デバイス窓 (PEGC) | `backend_pegc.c:546` (`pgalloc_range_has_ram` で 15-16MB に RAM が無いこと)、`:586,600,724` (`paging_map_phys`) | probe の**中で** enable → map → VRAM 書き込み試験 | 無し | kernel | 無 |
| デバイス窓 (Cirrus Xe10) | `gfx/backend_cirrus.c:213` (RAM 無し検査)、`:335` (map)、`:162` (unmap) | **probe の後に** map (D33 の順に反する)。窓は最初の init で 1 回だけ張る | 無し | kernel | 無 |
| デバイス窓の PT | `paging.c:239-262` | 0xFE000000〜 の先頭 4MB の PT を静的 1 枚 (`aperture_pt_raw`) | — | kernel | 無 |
| hot-deploy の残骸 | コードは無し (2026-09-09 撤去、`memmap.h:436-446`)。**古い記述だけ残る**: `memmap.h:158`「ホットデプロイ窓の直下を削って取る」、`backend_pegc.c:685`「`sys_hotdeploy_base()` 側」(その関数は無い)、`CLAUDE.md` の Memory layout 表 (`0x8C000` の hot-deploy block — 実際は `V86_TEST_MAGIC_ADDR`、`kernel/v86.h:229` — と top の「Hot-deploy staging」) | — | — | — | — |

### 2-2. 観察 (設計に効くもの)

1. **物理ページに owner が無い。** 今の「所有」は bitmap 1 bit と、呼び手が持つ番地 (`s_bb_phys`、AS 構造体、attach 表、`backing_phys`) と、アプリなら PTE の辿り直しだけ。R5 の「owner=その ID のページが 0」は今は数えられない。
2. **8MB は legacy に落ちる理由が「workspace を 0xC00000 より上に置く」不変条件** (`memory_boot.c:202-209`、[APP_BAND_PDE](../memory/APP_BAND_PDE.md) §2)。2 枚 PDE のアプリが identity のアプリ帯 (0x400000〜0xBFFFFF) を USER で写すと master の PT を書き換えられる、という防御。T2 でアプリ帯が 0x80000000 へ出れば不要になるが、T1 は T2 の前なので**この不変条件を保ったまま 8MB をモデル経路にする**必要がある (§3-3)。
3. **`irq_in_irq` (`kernel/irq.c:25`) は共通スタブ `irq_dispatch` (`:132,152`) の中しか数えない。** IRQ0 (タイマ)・IRQ1 (キーボード)・IRQ4・FDC・マウスは専用スタブ (`isr_stub.asm:468,505,554,603,680`) で、深さに入らない。CTRL+STOP の畳み (`ring3_abort_check`、`isr_stub.asm:538`) は IRQ1 スタブの EOI の**後**・`iretd` の前で呼ばれ、longjmp で戻らない。R1 の「割り込みの入れ子深さ」は今の変数では測れない (§3-5)。
4. **例外フレームの中でも回収している**: #PF / #GP のアプリ kill (`kernel/isr_handlers.c:259,358` → `ring3_fault_kill`) は AS 破棄 → longjmp を例外ハンドラの中で行う (`isr_handlers.c:18`)。R1 は ISR を名指しするが、同じ形 (割り込みフレームの上の回収) が例外経路にもある (§5 T1-U3、Codex X12)。
5. **カーネル本体の残り予算は 13.0KB** ([`02_memory.md`](../../02_memory.md) §2-1、583.0KB / 596KB)。T1 は T3 (カーネル帯 3MB) より前なので、台帳のコードと kselftest の追加はこの 13KB に入れなければならない (§5 T1-R1)。
6. **DMA プールは今 64KB 境界をまたぐ** (0x2E8000〜0x2F7FFF)。D5 の「64KB 整列」は置き場の移動 (T3) を待つ。T1 の R4 の試験は「今の池で PCM 16KB + 82557 ≒16KB (`dma_pool.h:4`) が最悪の並びで入る」を見る (両側 32KB ずつなので入る)。
7. **Cirrus のコメントと paging の契約が食い違う**: `backend_cirrus.c:327-329` は「失敗しても範囲内の分は適用済み」と書くが、DEVICE_RESERVATION §6 は `paging.h:87-102` の map を「単一呼出し全件不変で失敗」と読む。T1e で map を probe の前へ動かすとき、どちらが正かを試験で確かめる (§5 T1-U5)。
8. **`paging_is_present(phys)` に物理番地を渡している**: `exec/exec.c:816,827` (`ring3_pd_range_writable`) は PD / PT の物理番地を `paging_is_present` (仮想を取る) に渡し、`:821,829` で `(const volatile u32 *)pd_phys` と読む。恒等の間は正しいが、P2V の監査で「paging.c の外で表を物理で辿る」唯一の箇所 (T1f で paging.c の関数へ寄せる)。

### 2-3. 物理ポインタの直接参照 — 監査の見積り (P2V / V2P の導入対象)

`kernel/ drivers/ gfx/ fs/ exec/ kapi/ lib/` を走査 (third_party・SQLite・fatfs・zlib・microtar・os32_lz4・userland は除く)。grep と目視で ±数件。

| 分類 | 向き | 件数 | 主な場所 |
|---|---|---:|---|
| VRAM / TVRAM / PEGC / 窓の定数キャスト | P2V | 59 | `console.c` 15、`gfx_core.c` 18、`gfx_vram.c` 8、`backend_pegc.c` 7、`v86_gcap.c` 4、`kernel.c` 2、`ime.c` 2、`isr_handlers.c` 2、`backend_cirrus.c:345` 1 |
| BDA / IVT (0x000〜0x5FF) | P2V | 6 (+ V86 のページ 0 経由 4) | `sysclk.c:29`、`kbd.c:264`、`backend_pegc.c:134` (`bios_flag`、呼び手 5)、`memory_boot.c:79`、`v86_bios.c:265,278` |
| `MEM_*` 固定番地のキャスト | P2V | 16 | `kernel.c:608`、`bootinfo.c:59`、`kselftest.c:790`、`gfx_core.c:17-20`、`backend_pc98.c:154`、`lib/utf8.c:32`、`kcg.c:52-55,215`、`shlib.c:77` |
| PD / PT を物理で触る | P2V | 21 (+`paging_is_present(phys)` 2) | `paging.c` 19 (`:400,418,441,453,717,737,794,799,836,889,892,922,1022,1083,1099,1100,1111,1129,1134`)、`exec.c:821,829` |
| PFN × PAGE_SIZE → ポインタ (恒等の検証・起動前の検出) | P2V | 5 + 4 | `pgalloc.c:126,170,211,219`、`memory_boot.c:217`、`memory_boot.c:79,89,90,104` (`boot_ptr` — P2V の種になる既存の唯一の関数、`memory_boot.c:26`) |
| 物理確保の結果 → ポインタ | P2V | 7 | `backend_pegc.c:704,705,847,932`、`shlib.c:210,285`、`dma_pool.c:46` |
| ポインタ → PDE / CR3 / 恒等比較 | V2P | 10 | `paging.c:253,262,331,386,413,442,457,626,1097`、`pgalloc.c:103,114` |
| カーネル静的ポインタを PTE の物理に | V2P | 3 | `exec.c:158,1964` (トランポリン)、`gfx_core.c:148` (`bb_base`) |
| DMA へ渡すポインタ | V2P | 6 | `fdc.c:63,748,851,970` (BSS `s_fdbuf`)、`dma_pool.c:56,68` |
| HostDrv の hypercall に渡すポインタ | V2P (要確認) | 24 | `fs/hostdrvfs.c:107,144-381` — NP21/W が線形番地と物理番地のどちらで読むかはコードに書かれていない (§5 T1-U6) |
| 恒等写像の呼び出し (virt と phys に同じ値) | 見直し | 33 | `paging.c`、`shm.c`、`shlib.c`、`v86_mem.c`、`v86_bios.c`、`pgalloc.c:217`、`backend_pegc.c`、`backend_cirrus.c`、`exec.c:1352-1964` |
| **仮想なので変えないもの** | — | ≈35 | V86 のゲスト線形 (`v86.c:98 v86_ptr` ほか)、リンカ由来 (`KHEAP_BASE`、`MEM_SHM_BASE`、トランポリン)、ユーザ / シェル帯、ポインタ算術 |

**合計: P2V / V2P の書き換え ≈ 150 件、見直し ≈ 45 件、対象外 ≈ 35 件。** P2V / V2P という名の既存関数は無い。近いもの: `boot_ptr` (`memory_boot.c:26`)、`mmio_w8/16`・`bios_flag` (`backend_pegc.c:105,111,127`)、`dma_pool_alloc` の `phys_out`、`paging_verify_identity` (`paging.c:372`、P = V を表明する)。

---

## 3. 設計

### 3-1. 台帳のデータ構造

台帳は 4 層。**既存の `physmem` / `pgalloc` の表をそのまま下の 2 層に使い**、上に owner と区間の表を足す (新しい allocator は作らない)。

| 層 | 持つもの | 実体 | 新規か |
|---|---|---|---|
| L0 物理地図 | RAM の存在と種別 (RAM / RESERVED / MMIO / UNKNOWN)、source (LEGACY / MACHINE) | `struct physmem` (`physmem.h`)、起動時に 1 回作って凍結。`pgalloc` の私有の写し `device_boot_map` (`pgalloc.c:25`) | 流用 |
| L1 割当可否 | eligible bitmap / allocated bitmap (PFN ごと 1 bit ずつ) | `pgalloc.c` の 2 bitmap | 流用 |
| L2 **owner** | PFN ごとの owner 番号 (`u8`、0 = 空き)。**不変条件: allocated ⇔ owner ≠ 0** | 新設。metadata の同じ塊から切り出す (`pgalloc_metadata_bytes` を 2 bit + 8 bit / PFN に) | 新規 |
| L3 **区間の表** | PFN 半開区間ごとの種別・owner・キャッシュ属性・資源レコード。bitmap の管理範囲外 (MMIO、4GiB 端) も完全な範囲で持つ | 新設。固定長の配列 (動的確保なし) | 新規 (`device_ledger` の後継) |

**owner の表** (`struct ledger_owner`、固定長 64 本 = owner 番号 1〜63。番号 0 は空き)

| 項 | 内容 |
|---|---|
| `kind` | **AS** (アプリの ID。終了で全部回収、R5 のゼロ検査の対象) / **PERSIST** (kernel・boot・bundle・shlib・gshell。AS の終了経路は触らない、R5 (b)) / **MODULE** (モジュール名。永続だが R7 の一括回収の単位) / **DEVICE** (kernel 寿命の安定 ID。MMIO の永久予約だけを持ち、**RAM の回収 API はこの種別を拒否**する — X4 の「永久予約とモジュール回収の分離」) |
| `id` | AS なら app ID (`appslot`)、他は固定の列挙 |
| `name[8]` | `mem` の表示用 |
| `pages` | 持っている RAM のページ数 (L2 と一致することを kselftest が見る) |
| `alloc_irq`, `free_irq` | R1 の件数 (§3-5) |

固定の番号: 1 = kernel、2 = boot、3 = bundle、4 = shlib、5 = gshell、6 = staging (集積域)。AS と MODULE と DEVICE は必要時に割り当てる。AS owner は `exec` が AS 作成時に取り、AS 破棄で `pages == 0` を確かめてから返す。

**区間の表** (`struct ledger_region`、固定長 48 本)

| 項 | 内容 |
|---|---|
| `first`, `end` | PFN 半開区間 (`end` = 1048576 で 4GiB 端を表せる。バイトの排他端は使わない — 既存 `physmem` と同じ) |
| `type` | FIXED (カーネル帯・SQLite 帯・シェル帯・低位の固定用途)、RESERVED (15〜16MB の登録の無い部分)、STAGING (集積域)、BUNDLE (同梱域)、DMA (DMA プール)、MMIO、SURFACE_BACKING (SURFACE が参照する固定 RAM、planar BB) |
| `owner` | owner 番号 |
| `cache` | WB / UC (MMIO と Cirrus の面は UC = PCD。lease の PTE はここから引く、§2-2「キャッシュ属性は台帳から」) |
| `flags` | PERMANENT (解除 API なし)、OUTSIDE (bitmap の管理範囲 `[0, limit_pfn)` の外にかかる) |
| `res` | 資源レコードの番号 (MMIO だけ、0 = 定数由来) |
| `span_set` | 同じ一括 commit で入った区間の組の番号 (同 owner の完全一致判定に使う — `pgalloc_device_reserve` の冪等規則) |

**検証済み資源レコード** (`struct ledger_resource`、固定長 16 本、X4)

| 項 | 内容 |
|---|---|
| `bus` | PCI / CBUS (C バスの窓) / FIXED (機種固定、PEGC) |
| `bdf`, `vendor`, `device`, `revision`, `bar`, `raw_bar` | PCI のとき (列挙の読み取り値。`drivers/pci.c:206` は BAR を読むだけで書かない) |
| `decode_first`, `decode_end` | **予約範囲** = 装置が応答する decode aperture 全体 (PFN) |
| `map_first`, `map_end` | **写像範囲** = 確認済みの必要部分 (`paging_map_phys` で sup+PCD に張る範囲) |
| `width_basis` | decode 幅の根拠: SIZING (BAR の sizing を実施)、DATASHEET (資料)、GLUE_CONST (glue の定数、例 Xe10 の NP21/W 4MiB)、PROBE_UNVERIFIED (未検証 — **予約権限にしない**) |
| `boot_gen` | 起動世代 (GUI 境界での同一性の再確認に使う — 再確認そのものは P4) |

面範囲 (USER に lease する面) は SURFACE の側に持つ (下)。**3 つの範囲は別の表・別の欄** — 同じ `{base, size}` を予約権限にも写像にも lease にも使わない (X4)。

**SURFACE** (`struct ledger_surface`、固定長 8 本。T1 は型と表と登録だけ、lease の付け外しは T2)

| 項 | 内容 |
|---|---|
| `owner` | boot → gshell (BB)、kernel (TVRAM・表示面) |
| `backing` | RAM (池から確保した PFN 区間) / FIXED_RAM (planar BB 0x6A000) / VRAM (0xA0000〜) / MMIO (PEGC リニア窓、Cirrus の面) |
| `first`, `npages` | **ページ単位で占有** (面の末尾をページへ切り上げ、同じページを他の用途と共有しない、§2-2) |
| `width`, `height`, `pitch`, `format`, `planes` | 面の形 (planar は 4 プレーン) |
| `cache`, `perm_max` | 区間の表から引いた属性、貸せる権限の上限 |
| `gen` | 世代 (再 init・モード変更で面の意味を変えるときに上げる。既存 lease があれば先に revoke — T2) |
| `lease_count` | T1 の間は常に 0。**物理の解放は `lease_count == 0` かつ owner が返したときだけ** |

**大きさ (U1 の一部)**: owner 64 × 16B + 区間 48 × 24B + 資源 16 × 48B + SURFACE 8 × 40B ≈ 3.2KB (固定)。L2 は 1B / PFN (8MB = 2KB、17MB = 4.25KB、64MB = 16KB、2GB = 512KB)。L1 と合わせて 1.25B / PFN。**全部 metadata の塊に置き、BSS には置かない** (§5 T1-R1 の予算)。実測は T1a の受入で報告する。

### 3-2. API (カーネル内部だけ。KAPI は変えない)

**原則** (既存の `pgalloc` / `physmem` の流儀をそのまま): 物理は **PFN (u32) で返し、成功は別の戻り値** (PFN 0 と最後のページを表せる)。全 mutator は **全部検査してから commit、commit の後に失敗しうる処理を置かない**、失敗時は台帳・bitmap・統計・出力引数を全部不変。IF は `irq_save` / `irq_restore` で保存。**owner は必ず引数で渡す** — 暗黙の `res_owner_get()` を使わない (§3-5-3 の 2.「owner の安定」の下準備)。

| API (名は案、T1a で確定) | 意味 | 置き換える今の口 |
|---|---|---|
| `ledger_owner_new(kind, id, name, *owner)` / `ledger_owner_retire(owner)` | owner 番号の取得 / 返却 (AS は `pages == 0` でないと返せない) | — |
| `pgalloc_alloc_n_owner(owner, n, first, end, flags, *pfn)` | n ページ連続 (flags: TOP_DOWN / BOTTOM_UP)。`alloc_n_pfn` (`pgalloc.c:248`) に owner の書き込みを足す | `pgalloc_alloc_n` / `_page` / `_n_range` / `_n_pfn` |
| `pgalloc_free_n_owner(owner, pfn, n)` | **全ページが owner のものであるときだけ**解放 (他 owner のページが混じれば全件不変で拒否し `bad_free` を数える) | `pgalloc_free_n` / `_page` / `_n_pfn` |
| `ledger_transfer(pfn, n, from, to)` | 全ページが `from` のものなら一括で `to` へ (bundle → モジュール、boot → gshell) | — |
| `ledger_reclaim_owner(owner, *pages)` | owner のページを全部返す (R5 の AS 終了、R7 のモジュール失敗)。**DEVICE owner、`lease_count > 0` の SURFACE を持つ owner は拒否** | — (今は PTE を辿るだけ) |
| `ledger_claim_fixed(owner, first, end)` | 固定番地を owner で押さえる (shlib 帯) | `pgalloc_mark_used` |
| `ledger_register_region(type, owner, first, end, cache, flags)` | 起動時 (live AS = 0) だけ。FIXED / RESERVED / STAGING / BUNDLE / DMA の登録 | — |
| `ledger_reserve_set(owner, spans[], n, res)` | **MMIO / 永久 RAM の一括予約** — `pgalloc_device_reserve` の核を流用 (正規化 → 他 owner・固定用途・allocated・既存装置資源と全件照合 → 一括 commit、同 owner の完全一致だけ冪等、管理範囲外も完全な範囲を記録し bitmap 操作は交差だけ)。`res` は資源レコード (PROBE_UNVERIFIED は拒否) | `pgalloc_device_reserve` / `sys_device_reserve_core` |
| `ledger_surface_create(owner, backing, first, npages, geom, *sid)` / `ledger_surface_transfer(sid, to)` | SURFACE の登録と移譲 | — |
| `ledger_owner_pages(owner)`、`ledger_dump(cb)` | `mem` と kselftest | `pgalloc_free_pages` は残す |
| `ledger_arena_top()` | **T1 の間だけ**: CPL=0 子の identity アリーナの上端 (§3-6)。T2 で消す | `sys_usable_mem_end` の `sys_top_reserved` 部分 |

呼び手の書き換え (T1a): `paging.c` の AS の PD / PT (owner = その AS) と `reserve_table` (kernel)、`exec.c` の `app_map_region` と teardown (AS、最後に `ledger_reclaim_owner` で取り残しを掃除して件数を診断に出す)、`shlib.c` の帯 (shlib) と data 複製 (AS)、`v86_mem.c` のバッキング (V86 セッションを起動したアプリの AS)、`backend_pegc.c` の BB (boot、T1e で SURFACE へ)。**owner を取らない旧 API は T1a の最後で消す** — 取り残しをリンクエラーで見つける。

### 3-3. 起動順 — 台帳をいつ作り、誰が何を登録するか

T1 は起動順を大きく変えない (ルートマウントを `paging_init` の前に置いたまま。U19 と 2 段階の起動順は T5b)。変わるのは ③ の中身と ⑥ の新設だけ。

| # | 段 (今の位置) | T1 での中身 | live AS |
|---|---|---|---|
| ① | `memory_boot_detect` (`kernel.c:191`、`paging_init` の前) | 変更なし。**高位 RAM の登録源はここだけ** — `physmem_add_trusted(MACHINE)` の呼び手が `memory_boot.c` だけであることをホスト検査で見る (D32) | 0 |
| ② | `paging_init` (`kernel.c:505`) | 変更なし (8MB 用の表の置き場を present にするのは ③) | 0 |
| ③ | `memory_boot_init` (`kernel.c:557`) → **台帳の初期化** | 地図 (L0) → metadata の置き場を決める (下) → L1/L2 → 区間の表に固定用途を登録: 低位 (NULL ガード・フォント・Unicode・planar BB (SURFACE_BACKING)・bootinfo・mailbox・VRAM・ROM)、カーネル帯、SQLite 帯、DMA プール、固定 PT、カーネルスタック、シェル帯、15〜16MB (RESERVED)、`[MEM_PHYS_RAM_CEILING, 4GiB)` (MMIO)。**集積域・同梱域の規則**: bootinfo が同梱エントリを申告していれば `MEM_BOOT_BUNDLE_*` を owner=bundle で予約、`MEM_BOOT_STAGING_*` は展開済みなので池へ (T1 の時点ではローダが使わないので両方とも空振り — 規則とホスト試験だけ) → `stage_online` | 0 |
| ④ | `dma_pool_init` (`kernel.c:567`) | 池は台帳の DMA 区間の上。`dma_alloc` の口 (§3-7) | 0 |
| ⑤ | `kselftest_run` (`kernel.c:574`) | 台帳の不変条件 (allocated ⇔ owner ≠ 0、owner の `pages` の合計、区間の非重複、R4 の最悪の並び) | 0 |
| ⑥ | **新設: gfx の識別 + MMIO 登録 + 写像 + BB 確保** (`exec_init` の前、`kernel.c:597` の手前) | `GFX=` の読み取り (今は `kernel.c:673-690`、`exec_init` の後) を**ここへ前倒し** (ルートは `kernel.c:434` でマウント済み) → 副作用のない識別 (§3-8) → 候補の窓を `ledger_reserve_set` (owner = DEVICE) → `paging_map_phys` (sup+PCD、全戻り値を検査) → BB を池から連続確保 (owner = boot、SURFACE) | 0 |
| ⑦ | `exec_init` → … → `boot_splash` → `gfx_prepare_backend` (`kernel.c:696,707`) | probe / enable は今の位置のまま (GUI 境界への移動は P4)。**probe は ⑥ で張った写像と確保済みの BB を使い、自分では予約も写像もしない** | 0 |
| ⑧ | `shlib_init` (`kernel.c:712`) | 帯を `ledger_claim_fixed(shlib)`、失敗経路は `pgalloc_free_n_owner(shlib, …)` | 0 |
| ⑨ | GUI の開始 (`kernel.c:785` の `is_gui` の分岐、`exec_run(gshell)` の直前) | `ledger_surface_transfer(BB, gshell)`。2 回目以降 (GUI → CUI → GUI) は同じ owner なので無操作 — BB は返さず保持 (R5 (b)) | 0 |

**metadata (L1 + L2 + 表) の置き場** (legacy fallback の撤去、D32):

| 構成 | 置き場 | 理由 |
|---|---|---|
| 低位 RAM の末尾に `metadata + workspace` が入り、下端が `MEM_APP_BAND_MAX_TOP` (0xC00000) 以上 (≒ 13MB 以上の機。17MB・64MB) | **今と同じ** (低位 RAM の末尾、`memory_boot.c:216-219`) | 回帰を増やさない |
| それ以外 (8MB・9MB・12MB 前後) | **DMA プールの上の予約 12KB `[0x2F8000, 0x2FB000)`** を present / supervisor / RW にして使う (3 ページ = metadata 2 + workspace 1。15MB でも L1 + L2 + 表 ≈ 7.7KB で入る) | 番地が固定 (SQLite の伸び代 `MEM_KERNEL_RESV` 下側 44KB を食わない)、アプリ帯 (0x400000〜0xBFFFFF) の外なので §2-2 の 2. の不変条件を保つ、BSS を増やさない。T3 でカーネル帯を組み直すとき KERNEL_SLACK の中へ移す |

どちらの置き場も `sys_memory_bootstrap_model` → `sys_memory_stage_online` の同じ経路を通す (`pgalloc_init_layout` の「metadata は legacy 上端に接する」の制約を「登録済みの固定区間の中」に一般化する)。**legacy の `pgalloc_init` と `legacy_metadata` は消す。** 8MB は高位 RAM が無いので stage は「全 eligible が恒等で張られていることの検証」だけになる (`pgalloc.c:209-211`)。

### 3-4. P2V / V2P の定義と検査

**定義** (`include/memmap.h` に 1 か所、C11 の `static inline` — T0 で gnu11 になった):

```c
/* v3: 恒等の supervisor 領域 (カーネル帯・シェル帯・池・低位・MMIO の恒等窓) だけ。
 * アプリ帯・lease 窓の番地は渡さない (as_va_to_pa / SURFACE から引く)。 */
static inline void *P2V(u32 pa)            { return (void *)(uptr)pa; }
static inline volatile void *P2V_IO(u32 pa){ return (volatile void *)(uptr)pa; }
static inline u32  V2P(const volatile void *va) { return (u32)(uptr)va; }
```

- `boot_ptr` (`memory_boot.c:26`) の「GCC の NULL 付近参照の誤検出を隠す」空 asm は `P2V_IO` の側に寄せる (BDA 0594h・0000:0400h 系で同じ警告が出うる)。**`paging_init` の前でも使える** (恒等なので)。v4 で高位化するとき、ページング前の P2V は別名 (`BOOT_P2V`) に分ける — T1 では区別の印だけ付ける (呼び手を `P2V_BOOT` と名付けて数える)。
- **アプリ帯・lease 窓は別の関数**: `as_va_to_pa(as, va, *pa)` (PTE を辿る。今 `exec.c:816-829` にある PD / PT の物理歩きを `paging.c` へ寄せて作る) と SURFACE の `first` (§3-1)。`V2P` にはこれらの番地を渡せない (検査する、下)。
- **DMA には物理の記述子を渡す**: `dma_alloc` の戻りは `{pa, va}` の組 (§3-7)。FDC の BSS バッファ (`fdc.c:54`) は `V2P(s_fdbuf)` にし、`V2P` の結果に 16MB 未満・64KB 非またぎの検査をかける (今は `fdc_buf_layout` `fdc.c:63` が見ている)。
- **恒等を表明している箇所** (`paging_verify_identity`、`kselftest.c:884-885` の `pa == (u32)a`、`pgalloc.c:114`) は包まずに意味を書き直す: 「`P2V(pa)` と一致する」へ。

**検査** (`make check` に入れる):

- 規則 ID を [`../../CONSTRAINTS.md`](../../CONSTRAINTS.md) に新設 (案: **[C5]「物理番地をポインタにするのは `P2V`、ポインタを装置・PTE・CR3 に渡すのは `V2P`」**)。`tools/check_constraints.py` は今 **ID の整合だけ**を見る (正典と `CLAUDE.md` の突き合わせ) ので、ID を足せばそこで整合が検査される。
- **走査は新しい `tools/check_p2v.py`** (`make check-p2v`、`check` の依存へ)。形は既存の `tools/check_le_access.py` と同じ 2 段:
  1. **文字列検査** — `kernel/ drivers/ gfx/ fs/ exec/ kapi/ lib/` で、ポインタ型へのキャスト `(T *)` / `(volatile T *)` / `(void *)` が (a) `include/memmap.h` の番地定数 (`MEM_*_BASE`、`MEM_*_ADDR`、`TVRAM_*`、`VRAM_PLANE_*`、`PEGC_*_BASE`、`BIOS_WORK_*`) か、(b) 名前が `*_phys` / `phys` / `pa` / `paddr` / `pfn` の式に当たっていたら、`P2V(` の引数でない限り違反。逆向き: `(u32)` へのキャストが PTE / CR3 / DMA の口 (`paging_*` の物理引数、`dma_chan_setup`、`dma_setup`、`arch_mmu_load_root`) に直接渡っていたら違反。**アプリ帯・lease 窓の名前** (`RING3_*`、`MEM_EXEC_LOAD_ADDR`、`MEM_APP_BAND_*`、`MEM_LEASE_*`、`uva`、`user_*`) を `V2P(` に渡したら違反 (X7-3)。
  2. **例外一覧** — `tools/check_p2v_allow.txt` に `file:関数:理由` で列挙 (V86 のゲスト線形、リンカ由来の仮想、`kmalloc` の算術など §2-3 の「対象外」)。行番号でなく関数名で持つ (行がずれても壊れない)。
- 文字列検査は取りこぼし得る (2 行に分けたキャスト)。それは T1f の受入の**変異試験**で測る: 既知の違反 3 形 (定数キャスト・`*_phys` キャスト・`V2P(RING3_…)`) を写しの木に注入して検査が落ちることを見る (`tools/tests/mutpar.py` の流儀、実物のソースは書き換えない)。

### 3-5. 割り込み中の確保・解放を数える (R1)

- **深さの変数を 1 本にする**: `kctx_irq_depth` (新設、`irq_in_irq` を置き換える)。**全部の IRQ スタブ** (`isr_stub.asm` の共通スタブと IRQ0 / 1 / 4 / FDC / マウスの専用スタブ) の入口で +1、`popad; iretd` の直前で −1 (アセンブラのマクロ `IRQ_ENTER` / `IRQ_LEAVE`)。IRQ1 の `ring3_abort_check` (`isr_stub.asm:538`) と V86 の脱出 (`:480-483`、`:521-524`) は `IRQ_LEAVE` の**前**に置いたまま — つまり深さ > 0 で走る (R1 の「割り込みフレームの上で走る `ring3_abort_check`」を数えるため)。
- **longjmp で抜ける経路は深さを戻す**: `ring3_kill_kind` (`exec.c:1516`)・`ring3_fault_kill`・`v86_exit_to_kernel` は longjmp の直前に、復帰点の `setjmp` で控えた深さ (通常 0) へ戻す。戻さないと以後の通常文脈が全部「割り込み中」に数えられる。
- **例外の深さは別に数える**: `kctx_exc_depth` (#PF / #GP の `ring3_fault_kill` 経路)。R1 の対象 (ISR) と区別して報告する (§2-2 の 4.、Codex X12)。
- **台帳の mutator** (`pgalloc_alloc_n_owner` / `_free_n_owner` / `ledger_transfer` / `ledger_reclaim_owner` / `ledger_reserve_set`) の入口で `kctx_irq_depth > 0` なら `ledger_irq_ops++`、owner の `alloc_irq` / `free_irq` を +1、最後の 1 件の `{op, owner, 呼び出し元 EIP}` を控える。例外深さも同様に `ledger_exc_ops`。**DMA プールの内部の割当は数えない** (R4: 固定プールの内側は利用時・IRQ からでよい)。
- **報告**: kselftest の 1 行 (件数) と `mem` の表示、カーネルシンボルとして公開 (`kselftest_pass` と同じく `/api/mem` で読む)。**T1 の受入は件数の報告** (CTRL+STOP・fault kill・PCM 再生中の CTRL+STOP でそれぞれ何件か)。T2 で経路を割った後に panic へ切り替える。

### 3-6. `sys_reserve_top` の撤去と CPL=0 子のアリーナ (T1 の間の暫定)

`sys_reserve_top` は「使用可能上限を下げて、その下を永久予約する」2 つの役を兼ねる (`sys.c:185-214`)。T1 は後者を台帳の連続確保 (owner = boot、TOP_DOWN) に置き換える。**前者 (上限を下げる) は CPL=0 の子のアリーナ (`exec_child_claim`、T2 で撤去) が要る**: CPL=0 の子は `[MEM_EXEC_LOAD_ADDR, sys_usable_mem_end())` を identity で丸ごと使い、スタックは上端から下へ伸びる。BB をこの中に置くと子が上書きする。

- T1 は `sys_usable_mem_end()` を「凍結した exec 上端」と `ledger_arena_top()` (= アリーナ内の**永続 owner の確保の最下端**、⑥ で 1 回だけ決めて凍結) の小さい方にする。BB を TOP_DOWN で取れば、結果は今の `sys_reserve_top` と同じ区間になる (回帰なし)。
- `pgalloc_mark_used` による claim は owner (= CPL=0 子の暫定 owner) 付きの `ledger_claim_fixed` にし、**他 owner のページが混じれば claim を拒否** (今は既割当に冪等で、release の `pgalloc_free_n` が BB のページまで返しうる — 今は BB が永久予約で eligible でないので起きないが、owner 付きにすれば構造で防げる)。
- T2 で CPL=0 子と claim を消すとき、`ledger_arena_top` も消す。

### 3-7. `dma_alloc` と 64KB 境界 (R4、D5、[HW2])

- 口: `int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)` / `int dma_free(const struct dma_buf *b)` / `dma_mark_leaked`。`struct dma_buf { u32 pa; void *va; u32 size; }`。`limit` は物理の上限 (ISA の 8237 は 16MB = `DMA_PHYS_LIMIT`)、**64KB をまたがないことは常に条件** ([HW2])。失敗は負 (`OS32_ERR_*`)、`*out` は不変。
- 実体は **既存の `dma_pool_state_alloc` (`kernel/dma_pool_math.c`、ホスト試験つき) をそのまま使う** — 最初適合 + 整列 + 64KB またぎの検査は既にある。足すのは `limit` の検査と `{pa, va}` の組 (va = `P2V(pa)`) だけ。`dma_pool_alloc` の呼び手は PCM (`pcm_cs4231.c:525`) と kselftest だけなので置き換えて消す。
- **池を触らない** (固定の DMA 区間の内側だけ)。IRQ から呼べる性質 (`dma_pool.c:3-5`) は保つ。
- kselftest (R4): 「PCM リング 16KB (`PCM_RING_BYTES`) + 82557 CB/RFD ≒16KB の最悪の並び」— 32KB ずつの 2 半面に 16KB + 16KB を、先に 8KB の穴を開けた状態も含めて置けること、どの結果も 64KB をまたがず `limit` 未満であること。T3 で池が 0x3E0000 (64KB 整列) へ移っても同じ試験が通る形にする (池の番地を試験に焼かない)。

### 3-8. gfx: 副作用のない識別 → 予約 → 写像 → (probe) と BB

⑥ の中身 (§3-3)。**予約・写像は識別だけで決め、probe を呼ばない** (D33)。

| 候補 | 識別 (副作用なし) | 予約 (`ledger_reserve_set`、owner = DEVICE) | 写像 (probe の前) | BB |
|---|---|---|---|---|
| PEGC | BDA 0x045C bit6 / 0x0597 bit2 を読むだけ (`backend_pegc.c:134 bios_flag`、今も probe の最初に読んでいる)、かつ 15〜16MB に RAM 登録が無い (`pgalloc_range_has_ram`) | `PEGC_LINEAR_BASE` の 512KiB 全体 (表示 300KB だけにしない、DEVICE_RESERVATION §3)。資源レコード bus = FIXED、width_basis = DATASHEET | 窓全体を sup+PCD。今 `pegc_probe` の中にある map (`:586`) を外へ | 300KB (`MEM_GFX_BB8_SIZE`) を池から連続 TOP_DOWN、SURFACE (owner = boot) |
| Cirrus (Xe10) | **副作用のない識別手段が無い** (ID の読み出しが index OUT、`wab_glue_xe10.c`)。T1 は「`GFX=cirrus` / `auto` で、機種が 9821 系」までを識別とみなし、**予約は存在の証明ではない**と明記 (DEVICE_RESERVATION §6) | 銀行窓 `[0xF60000, 0xF68000)` + リニア窓 `[0xFE000000, 0xFE400000)` (NP21/W の 4MiB) を**同一の一括 commit**。bus = CBUS、width_basis = GLUE_CONST | リニア窓を sup+PCD (今 `backend_cirrus.c:335` の probe 後 → 前へ)。probe が失敗しても予約と写像は永久保持 | 0 (面はカード VRAM の中) |
| planar | 常に | — (VRAM は固定区間) | — | 固定 0x6A000 を SURFACE (FIXED_RAM) として登録 |

- **BB の量は候補の最大** (`GFX=` と識別で残った候補のうち): auto で PEGC 候補が残れば 300KB。Cirrus が probe に失敗して PEGC へ落ちても確保済みの 300KB の内側 (X7-1「足りない側へ落ちる組み合わせは無い」を、候補の最大を取ることで保証する)。
- BB の中身は確保の直後に 0 で埋める (`backend_pegc.c:699-704` の理由 — CPL=3 に USER で写るので前の中身を見せない — は T2 までそのまま生きる)。
- `pegc_reserve_backbuffer` は「台帳から受け取った SURFACE を使う」だけになり、`sys_reserve_top` と `pgalloc_mark_used` の呼び出しが消える。

---

## 4. 段の分割と受入

各段は単独で着地・回帰できる (前の段だけを前提にする)。完了条件はどの段も `make check-changed` rc=0 (コーダー)、着地で PM が `make all` + `make check` を 1 回。

### 4-0. 3 つのメモリ構成の確かめ方 (NP21/W の ini は [D2])

| 構成 | 手段 | 新しい ini |
|---|---|---|
| **8MB** | 既存の live 変更ツールの preset `ram-8mb` (`ExMemory 7`、スキル `os32-emu-config` §1)。今は legacy fallback を通す構成 — T1b の後はモデル経路になることを見る | 不要 (preset の適用は PM 所有で [D2] の承認対象) |
| **17MB** (低位 15MB + 高位 1MB、アドレス上端 17MB) | preset `ram-15mb` (`ExMemory 16`)。名前は「15MB」だが検出量は 17408KiB (`memory_boot.h:18-28`) | 不要 |
| **64MB** | **実機 Ra266** (64MB、[実機の物理操作は個別承認])。NP21/W には 64MB の preset が無いので、`ram-32mb` (`ExMemory 33`) と `ram-128mb` (`ExMemory 129`) で挟む | 不要。NP21/W で 64MB ちょうどが要るなら preset `ram-64mb` の追加はツールの拡張 = 別タスク + [D2] (推奨しない — 32MB / 128MB で境界は覆える) |
| (参考) 9MB | 既存の試験用 ini `np21x64w-9mb.ini` (`ExMemory 8`、POLICY_DEBUG §4) | 不要 |

判定は**文言でなく番地**で読む: 新しい `kernel.map` の `pgalloc` の状態 (`model_mode` / `online`) と台帳の owner 表を `/api/mem?space=phys` で読み、`ledger_irq_ops` も同じく読む ([V4]、CLAUDE.md の Known Gotchas「kselftest は新しい kernel.map の番地で読む」)。

### 4-1. T1a — 台帳の核: owner・区間の表・R1 の計数・R5 / R7

| 項 | 内容 |
|---|---|
| 変更 | `kernel/pgalloc.[ch]` (L2 の owner 配列を metadata へ、owner 表、区間の表、owner 付き API、旧 API の撤去)、`kernel/physmem.[ch]` (変更なし予定)、`kernel/paging.c` (AS の PD / PT・`reserve_table`)、`exec/exec.c` (`app_map_region`・teardown・AS owner の取得 / 返却)、`kernel/shlib.c`、`kernel/v86_mem.c`、`gfx/backend_pegc.c` (BB の `mark_used` を owner 付き claim に — 置き場は T1e まで今のまま)、`kernel/isr_stub.asm` + `kernel/irq.[ch]` (`kctx_irq_depth`)、`exec/exec.c` の longjmp 経路 (深さを戻す)、`include/memmap.h` (P2V / V2P の定義 — 使うのはこの段で書き換えた呼び手だけ)、`kernel/kselftest.c`、`mem` コマンド (owner ごとの使用量) |
| ホスト試験 | 新規 `tools/tests/test_ledger.py` (`test_pgalloc_model.py` の実ソース ILP32 ハーネスを流用): owner 付き確保 / 解放 / 移譲 / 一括回収、他 owner 混在の解放は全件不変で拒否、DEVICE owner の回収拒否、owner 表満杯、allocated ⇔ owner ≠ 0、`pages` の合計、**合成モジュール owner の init 途中失敗 → 一括回収で 0** (R7)、**bundle → モジュール移譲の後の失敗は当該 owner だけ回収、bundle の残りは不変** (R7 往復 7 P2)。既存 `test_physmem` / `test_pgalloc_range` / `test_pgalloc_model` / `test_highram_stage` / `test_paging_bounds` は通ったまま |
| kselftest | 台帳の不変条件、AS を作って壊すと AS owner のページ 0 (R5 (a))、永続 owner の総量が起動直後から変わらない (R5 (b)・R6 の入口) |
| 変異 | owner 検査を外す (他 owner のページを解放できてしまう) / `IRQ_LEAVE` を 1 本抜く / 回収で DEVICE を拒否しない — をそれぞれ試験が止める |
| NP21/W 回帰 | 17MB (今の既定構成) で起動 kselftest 0 fail、gshell 起動、GUI アプリ 3 本の起動・終了 ×10 で AS owner 0、CUI の無限ループ中の CTRL+STOP・`fault_test` の fault kill・PCM 再生中の CTRL+STOP の**それぞれで `ledger_irq_ops` / `ledger_exc_ops` の件数を記録** (0 でなくてよい — 報告が受入、R1)、V86 (`v86 -t`) の起動と終了 |
| 大きさ | カーネル本体の増分を測って票に書く (予算 13.0KB、§5 T1-R1)。表を metadata に置くので BSS は減る側 (`legacy_metadata` 1KB と `device_boot_map` ≈ 1KB の移動)。owner / 区間 / 資源 / SURFACE の表の実サイズ (U1) |

### 4-2. T1b — モデル経路の一本化と物理側の定数

| 項 | 内容 |
|---|---|
| 変更 | `kernel/memory_boot.c` (fallback の撤去、置き場の 2 択 §3-3)、`kernel/pgalloc.c` (`pgalloc_init` と `legacy_metadata` の撤去、`pgalloc_init_layout` の置き場制約の一般化)、`kernel/sys.c` (`sys_memory_bootstrap_model` の layout 検査)、`kernel/paging.c` (`[0x2F8000, 0x2FB000)` を present にする — 8MB 型のときだけ)、`include/memmap.h` (**`MEM_POOL_BASE`** — `PGALLOC_BASE` 0x400000 を置き換える。**値は T1 では今の 0x400000 のまま、0x500000 にするのはシェル帯が 0x400000 へ来る T3** (§6 X9)、**`MEM_PHYS_RAM_CEILING` = 0x80000000** (D11。今は `MEM_DEVICE_APERTURE_BASE` = 0xFE000000、`memmap.h:389`)、`MEM_HIGH_RAM_BASE`、`MEM_BOOT_STAGING_BASE/SIZE` (0x500000 / 1MB)、`MEM_BOOT_BUNDLE_BASE/SIZE` (0x600000 / 1MB))、`tools/gen_memmap.py` (新定数と置き場の表示)、台帳初期化の固定区間の登録 (§3-3 ③) |
| D11 の影響 | 2GB 以上を RAM として登録しない (0594h の申告を 2GB で頭打ち、`memory_boot.c:82-83` の上限が変わる)。**`[2GB, 4GB)` の PCI BAR は MMIO として登録する** (U10) — 今は `[MEM_PHYS_RAM_CEILING, 4GiB)` を 1 回で MMIO にしている (`memory_boot.c:161`) ので、区間の表では「RAM ではない」と「装置の予約」を分ける (MMIO 型の固定区間 + 装置ごとの予約は T1d) |
| U24 | 0594h (1000000h 以降の容量、MB 単位) の意味を `docs/hw` の機種資料 (Bible + UNDOCUMENTED、矛盾時は UNDOCUMENTED) と照合し、結果を票に書く (資料そのものは著作権物なので写さない)。書き込み検証があるので結果が「違う」でも挙動は安全側 — 照合の結果で登録源を変えるかはそのとき判断 |
| ホスト試験 | `test_memory_boot.py` / `test_memmap_boot.py` / `test_highram_stage.py` を 8MB・9MB・12MB・15MB・17MB・64MB・2GB 超の申告で: **全部 model で ONLINE**、legacy の関数が存在しない、置き場の 2 択の境界 (0xC00000 ちょうど前後)、集積域 / 同梱域の規則 (bootinfo の申告あり / なし / 範囲外は拒否)、`physmem_add_trusted(MACHINE)` の呼び手が `memory_boot.c` だけ (grep の検査を試験に) |
| kselftest | `pgalloc_model_state() == PGALLOC_ONLINE` を起動のたびに見る (legacy に戻ったら fail) |
| 変異 | 置き場の境界を 1 ページずらす → リンク ASSERT か地図検査 (`gen_memmap.py --check`) か試験が止める (§7 全構成の行) |
| NP21/W / 実機 | **8MB (`ram-8mb`) で planar と PEGC の両方** GUI 起動と GUI アプリ 1 本、17MB (`ram-15mb`)、32MB / 128MB (`ram-32mb` / `ram-128mb`)、**Ra266 64MB** で起動 kselftest 0 fail と model ONLINE を番地で読む (D32 の受入) |

### 4-3. T1c — `dma_alloc`

| 項 | 内容 |
|---|---|
| 変更 | `kernel/dma_pool.[ch]` (`dma_alloc` / `dma_free` / `dma_mark_leaked`、`limit`、`{pa, va}`)、`drivers/pcm_cs4231.c` (呼び手)、`drivers/fdc.c` (BSS バッファの `V2P` と 16MB・64KB の検査を `dma_alloc` と同じ関数で — 置き場は変えない)、`kernel/kselftest.c` |
| ホスト試験 | `test_dma_pool.py` を拡張: `limit` 未満、64KB 非またぎ、失敗時 `*out` 不変、**PCM 16KB + 82557 ≒16KB の最悪の並び** (R4)、池の番地を試験に焼かない (T3 の移動でも通る) |
| kselftest | 同じ最悪の並びを実機の池で (今の `kselftest.c:871-920` の dmap の組を置き換え)。`pa == (u32)va` の表明は `va == P2V(pa)` へ |
| NP21/W 回帰 | PCM 再生 (TASK_PCM_CS4231 の E 試験のうち NP21/W で見られるもの)、FD の読み書き (FDC の DMA) |

### 4-4. T1d — MMIO 登録と検証済み資源レコード (D33・X4)

| 項 | 内容 |
|---|---|
| 変更 | `kernel/pgalloc.[ch]` (`pgalloc_device_reserve` を `ledger_reserve_set` の核に作り直す: 資源レコードの必須化、区間の表への記録、`span_set` による完全一致)、`kernel/sys.c` / `include/sys.h` (`sys_device_reserve_core` は `ledger_reserve_set` の薄い入口に、または撤去)、`drivers/pci.c` (列挙時に BAR ごとの資源レコードを作る口 — 実際に予約するのは使う driver。T1 では記録だけ)、`include/memmap.h` (必要なら区間の表の上限定数) |
| 仕様として残すもの (D33) | 複数 span の一括 commit、永久保持 (解除 API なし)、owner = kernel 寿命の安定 ID、同 owner・同一要求集合だけ冪等、管理範囲外も完全な範囲を記録し bitmap 操作は交差だけ、失敗時は出力も含め全不変、IRQ 保存 |
| 捨てるもの | exec アリーナ全域の禁止 (`pgalloc.c:431-435` の `MEM_EXEC_LOAD_ADDR` / `legacy_ceiling` / `fixed_end` の比較) — D3 でアリーナは消える側 (T2)。T1 の間は「他 owner と重なるか」+ 「固定区間と重なるか」の照合で足りる (CPL=0 子のアリーナは `ledger_arena_top` の上にしか予約が来ないことを §3-6 が保証する) |
| ホスト試験 | **`test_device_reservation.py` を流用** (`device_reservation_stage_host.c` の実ソース ILP32 ハーネス)。X4 の 6 項目を足す: **第二 span の衝突** (1 本目は通るが 2 本目が他 owner と重なる → 全体不変)、**台帳満杯** (区間の表・資源の表のそれぞれ)、**同 owner の部分一致の拒否**、**丸めによる衝突** (バイト表記を PFN へ切り上げた結果が隣とぶつかる)、**map / probe 失敗後の永久保持** (予約後に写像を失敗させても区間が残り、再試行は完全一致で冪等)、**モジュール回収後の予約保持** (`ledger_reclaim_owner(MODULE)` が DEVICE の区間と写像を消さない)。PROBE_UNVERIFIED のレコードは拒否 |
| kselftest | PEGC / Xe10 の定数由来の予約が区間の表に 1 組ずつ入っていること (T1e の後)、地図検査の期待値 (MMIO 登録は sup+PCD、15〜16MB の登録の無い部分は NP) のうち**台帳から期待値を引く部分** — 3 段の地図検査そのものは T2 |

### 4-5. T1e — gfx の識別・予約・写像、BB の SURFACE、`sys_reserve_top` の撤去

| 項 | 内容 |
|---|---|
| 変更 | `kernel/kernel.c` (⑥ の新設と `GFX=` 読み取りの前倒し、⑨ の移譲)、`gfx/gfx_core.c` (識別の口 `gfx_identify_candidates` — 副作用なし、`gfx_bb_phys_range` を SURFACE から)、`gfx/backend_pegc.c` (probe から map と BB 確保を外へ、`pegc_reserve_backbuffer` は受け取るだけ)、`gfx/backend_cirrus.c` (map を probe の前へ、失敗しても予約・写像は保持)、`kernel/sys.[ch]` (`sys_reserve_top` と `sys_top_reserved` の撤去、`sys_usable_mem_end` は §3-6)、`kernel/pgalloc.c` (SURFACE の表)、古い記述の掃除 (`memmap.h:158`、`backend_pegc.c:685`、`CLAUDE.md` の Memory layout 表の hot-deploy 2 行 — 正典の `02_memory.md` §2-1 は生成) |
| ホスト試験 | 識別 → 予約 → 写像 → probe の順を mock の I/O / 写像トレースで (DEVICE_RESERVATION §7 T3 の形を流用): PEGC の map 失敗・BB 不足、Cirrus の map 失敗・probe 失敗で、予約は保持・BB は公開されない・PC98 へ落ちる。**BB の量 = 候補の最大** (auto / pegc / cirrus / pc98 × PEGC 可 / 不可 の表) |
| kselftest | BB の SURFACE が 1 本 (owner = boot、GUI 後は gshell)、`sys_usable_mem_end() <= ledger_arena_top()` |
| NP21/W 回帰 | **3 バックエンド** (planar = `GFX=pc98`、PEGC、Cirrus = `cirrus-on`) で GUI 起動・描画・present、**GUI → CUI → GUI で BB の物理番地が同じ** (R5 (b))、Cirrus の probe を失敗させる構成 (`cirrus-off` + `GFX=cirrus`) で予約と写像が残り PC98 で起動、8MB × PEGC で GUI (T1b と同じ構成で再確認) |

### 4-6. T1f — P2V / V2P の全面適用と検査

| 項 | 内容 |
|---|---|
| 変更 | §2-3 の ≈150 件 (T1a〜T1e で書き換え済みの分を除く)、`exec.c:816-829` の表歩きを `paging.c` の `as_va_to_pa` 系へ、`paging_verify_identity` と kselftest の恒等表明の書き直し、`tools/check_p2v.py` + `tools/check_p2v_allow.txt` + `build/sdk.mk` (`check-p2v` を `check` へ)、`docs/CONSTRAINTS.md` と `CLAUDE.md` (新 ID、同じコミットで — `check_constraints.py` が整合を見る) |
| 受入の芯 | **コード列の同一**: 恒等なので、書き換えの前後で `objdump -d` の本体 (.text) が一致すること (差が出たら理由を列挙 — `static inline` の展開順や定数の畳み方)。これが「動作は変わらない」の証拠で、NP21/W の回帰は軽くてよい |
| 検査 | `make check-p2v` が 0 件 (**アプリ帯・lease 窓の番地に `V2P` を当てた箇所も 0**、§3-4)。変異: 3 形の違反の注入でそれぞれ落ちる |
| 対象外の扱い | V86 のゲスト線形・ページ 0 の二重の意味 (`v86_bios.c:283,1207` の `guest = (u8 *)0` はゲスト線形であり物理でもある) は例外一覧に理由付きで。HostDrv の 24 件は §5 T1-U6 の確認まで例外一覧 (「NP21/W が線形で読むか物理で読むか未確認」) |
| NP21/W 回帰 | 17MB で起動 kselftest 0 fail、GUI・CUI・V86・FD・HostDrv の一巡 |

### 4-7. T1 全体の受入 (TASK_MEMMAP_V3 §6 T1 の受入の要点との対応)

| T1 の受入の要点 (§6) | 段 |
|---|---|
| kselftest: 予約・割当・解放・失敗時回収 (モジュール init の途中失敗を含む) | T1a |
| 64KB 境界 (PCM リング + 82557 の同居) | T1c |
| 8MB / 17MB / 64MB のすべてでモデル経路 (legacy fallback 撤去、D32) | T1b |
| 高位 RAM の登録源は `memory_boot_detect` (0594h + 書き込み検証) だけ | T1b |
| MMIO 登録の一括 commit と失敗時の全不変 (流用試験) | T1d |
| X4: 第二 span の衝突、台帳満杯、同 owner の部分一致の拒否、丸めによる衝突、map / probe 失敗後の永久保持、モジュール回収後の予約保持 | T1d |
| P2V 検査 0 件 (アプリ帯・lease 窓の番地に `V2P` を当てた箇所も 0) | T1f |
| 割り込み中の台帳操作の件数を報告 | T1a |

---

## 5. リスクと未確認

### 5-1. リスク

| # | リスク | 手当て |
|---|---|---|
| T1-R1 | **カーネル本体の残り予算 13.0KB** に台帳のコードと kselftest の追加が入らない (T3 はまだ) | 表は全部 metadata に置き BSS を増やさない、`legacy_metadata` / `device_boot_map` / `pgalloc_init` の撤去で相殺、段ごとに増分を測る。入らなければ T3 の「固定 PT を画像の外へ」(BSS の `pd_raw` + `pt_raw` + `aperture_pt_raw` ≈ 40KB) だけを T1 の前へ切り出す (§6 X15) |
| T1-R2 | 8MB 型の置き場 `[0x2F8000, 0x2FB000)` 12KB に表が入らない (区間の表・資源の表を増やしたとき) | 表の本数を定数にして `STATIC_ASSERT` で 3 ページ以内を保証。溢れるなら表を KHEAP から取る案に倒す (KHEAP 外へは出ない、R3) |
| T1-R3 | 深さの変数を全 IRQ スタブに入れる asm の変更が、V86 の反射・`ring3_abort_check` の longjmp と噛み合わず深さが漏れる | 深さの漏れを kselftest と `mem` で見る (通常文脈で 0 であること)、変異 (`IRQ_LEAVE` を抜く) で試験が止まることを確かめる |
| T1-R4 | ⑥ で `GFX=` の読み取りを `exec_init` の前へ動かしたとき、`sysconfig_get_str` が `exec_init` 後の何かに依存している | T1e の最初に依存を確かめる (ルートは `kernel.c:434` でマウント済み)。依存があれば ⑥ を `exec_init` の直後・最初の AS より前に置く (live AS = 0 は保たれる) |
| T1-R5 | Cirrus の map を probe の前へ動かすと、今「最初の init で 1 回だけ張る」前提 (`backend_cirrus.c:320-331`) と二重に張る | 張るのは ⑥ の 1 回だけにし、backend の init からは map を消す。単独 CPL=3 アプリの `gfx_init` で PTE を supervisor で上書きする旧障害 (2026-09-06) が戻らないことを回帰で見る |

### 5-2. 未確認 (TASK_MEMMAP_V3 の U 番号との対応 + この票の分)

| # | 前提 / 測るもの | 対応 | どこで |
|---|---|---|---|
| U1 | owner / lease 台帳の実サイズ (16KB は推測) | 表 ≈3.2KB + 1.25B/PFN の見込み (§3-1) を実測で置き換える | T1a |
| U10 | PCI の BAR が 0x80000000 以上にある機種 | D11 で RAM の上限を 2GB にした後、`[2GB, 4GB)` の BAR は MMIO 区間 + 資源レコードで持つ | T1b / T1d |
| U24 | 0594h の意味を機種資料と照合 | T1b の作業に入れた | T1b |
| U19 | `paging_init` / 台帳をルートマウントの前へ | **T1 では触らない** (T5b)。T1 の台帳はルートマウントの後に作られるので、T5b は ③ を前へ動かすだけで済む形にする (③ が VFS に依存しないこと) | T5b |
| T1-U1 | 8MB 機でアプリの identity 写像が 0x2F8000 の表に届かないこと (PDE 0 はカーネル帯で supervisor) | kselftest の MM 検査 (`MM_RW` vs `MM_RWU`) に置き場を足す | T1b |
| T1-U2 | V86 バッキングの解放 (`v86_mem_teardown`) が IRQ フレームの中で走るか (V86 の脱出は IRQ0 / IRQ1 のスタブから longjmp) | R1 の計数で実測する | T1a |
| T1-U3 | 例外フレームの中の回収 (#PF / #GP の fault kill) を R1 と同じ扱いにするか | 数えて報告 (§3-5)。扱いは T2 で決める (Codex X12) | T1a → T2 |
| T1-U4 | Xe10 に副作用のない識別手段が無い — 予約を `GFX=` と機種だけで決めてよいか | 予約は存在の証明ではない (DEVICE_RESERVATION §6)。予約した窓に RAM 登録が無いことだけ確かめる | T1e |
| T1-U5 | `paging_map_phys` の失敗時の契約 (全件不変か、範囲内適用済みか) — `backend_cirrus.c:327-329` と DEVICE_RESERVATION §6 が食い違う | T1e のホスト試験で確定し、どちらかのコメントを直す | T1e |
| T1-U6 | HostDrv の hypercall (`fs/hostdrvfs.c`) が渡すポインタを NP21/W が線形番地で読むか物理番地で読むか | NP21/W ai-debug フォーク (`~/np21w-src`) の HostDrv 実装で確かめる。T1f では例外一覧に置く | T1f |
| T1-U7 | 文字列検査 (`check_p2v.py`) の取りこぼし率 | 変異試験で 3 形を測る。`-Wcast-align=strict` のようなコンパイラ側の検出手段は無い (恒等なので型が同じ) | T1f |

---

## 6. Codex に突き合わせる論点 (X9〜)

TASK_MEMMAP_V3 §8-4 の X1〜X8 の続き。T1a の着手前に 1 回 (ROLES §5 の書式、`codex exec -s read-only`)。

| # | 論点 | この票の案 |
|---|---|---|
| X9 | `MEM_POOL_BASE` の値を T1 で 0x500000 にするか、今の `PGALLOC_BASE` (0x400000) のまま名前だけ置き換えて T3 で 0x500000 にするか | 後者 (§4-2)。T1 では 0x400000〜0x4FFFFF が shlib 帯で、shlib が無いときは池へ返す今の挙動を保つため。0x500000 にすると shlib の無い構成で 1MB が死ぬ |
| X10 | 8MB 型の metadata の置き場を DMA プールの上の 12KB `[0x2F8000, 0x2FB000)` にする案 — アプリ帯の外・固定番地・BSS 非増加。別案 (a) KHEAP から取る、(b) BSS に 16MB 分を静的に、(c) 池の中 (アプリ帯の identity と重なるが owner = kernel で配らない) | 12KB 案。(c) は APP_BAND_PDE §2 の不変条件 (2 枚 PDE のアプリが identity を USER で写す) を T2 の前に崩すので不採用 |
| X11 | L2 (owner) を allocated bitmap と**別に持つ** (不変条件 allocated ⇔ owner ≠ 0) か、bitmap を owner 配列で置き換えるか | 別に持つ (既存のホスト試験のハーネスが bitmap の不変条件を見ているので、T1 の差分を小さくする)。一本化は T3 以降の掃除 |
| X12 | R1 の計数に**例外フレームの中の回収** (fault kill) を含めるか、IRQ と分けて数えるか | 分けて数える (`ledger_exc_ops`)。R1 の文面は ISR だけを名指ししているが、同じ形の反例 (回収の中の待ち) は例外経路にもある |
| X13 | 深さの計数を全 IRQ スタブに入れる (asm 変更) か、台帳の mutator の入口で「IF=0 かつ起動列でない」を代理にするか | 前者。代理は `irq_save` 区間の中からの呼び出し (paging の内部) を誤検出する |
| X14 | T1 の間の `ledger_arena_top` (CPL=0 子のアリーナ上端を台帳から求める暫定) で、`sys_reserve_top` の撤去と CPL=0 子の併存が安全か | §3-6 の 2 点 (BB は TOP_DOWN で今と同じ区間、claim は他 owner 混在を拒否) で足りるか |
| X15 | カーネル本体の予算 13KB が足りないとき、T3 の「固定 PT を画像の外へ」だけを T1 の前へ切り出してよいか (順序 T0 → T1 → T2 → T3 の例外) | 必要になったときだけ (T1a の実測で判断)。決定 (順序) を変えるので、その場合はユーザーに上げる |
| X16 | P2V の検査を `check_constraints.py` ではなく新しい `check_p2v.py` に置く — TASK_MEMMAP_V3 §3-4 の「`check_constraints.py` に検査を足す」の読み方 | ID は `CONSTRAINTS.md` に足して `check_constraints.py` が整合を見る、走査は `check_p2v.py` (既存の `check_le_access.py` と同じ形)。`check_constraints.py` は ID の整合だけの道具なので、走査を混ぜると役が 2 つになる |

---

## 7. ユーザー判断が要る点

**決定 (D 番号) を変えるものは無い。** 実装の途中で次の 2 つが起きたときだけユーザーに上げる:

1. **X15** — カーネル本体の予算 13KB に T1 が入らず、T3 の一部 (固定 PT を画像の外へ) を T1 の前へ動かす必要が出たとき (順序の例外)。
2. **64MB の NP21/W 構成** — 32MB / 128MB の preset で挟むのでは足りず 64MB ちょうどが要るとき、preset `ram-64mb` の追加 (ツールの拡張 + [D2])。この票の推奨は「不要 (Ra266 の実機で見る)」。

NP21/W の `ExMemory` の切り替え ([D2]) と Ra266 での確認 (実機の物理操作) は、各段の受入の時点で個別承認。
