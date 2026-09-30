# TASK_T1_LEDGER — T1: 物理地図と所有権台帳 (設計票)

> 状態: **実装中 (2026-10-01) — T1a・T1b 着地 (main `d7ac7a0`)、次は T1c**。NP21/W 回帰の結果は §4-2-N。CTRL+STOP・#GP / #DE / #UD の kill は 2026-10-01 に確認済み (§4-2-N の末尾)。未確認: PCM 再生中の CTRL+STOP (NP21/W の構成に PCM が無い)、実機 Ra266 64MB (実機エージェントに依頼中)。
>
> それまでの状態: **実装中 (2026-09-30) — T1b 実装済み、NP21/W 回帰は PM** (`wt/t1b`、コーダー `claude-opus-5-5`、結果は §4-2-R)。
>
> それまでの状態: **実装中 (2026-09-30) — T1a 実装済み、NP21/W 回帰は PM** (`wt/t1a`、コーダー `claude-opus-5-5`、結果は §4-1-R)。
>
> それまでの状態: **設計中 (2026-09-30) — 設計承認、T1a の実装待ち**。Codex (gpt-6-astra) の設計レビュー 3 往復 + ユーザー決定 (上限到達、残り 2 件を Codex の示した形で直し差分だけ確認) の確認で **Approve** (2026-09-30、`5ef0087`)。
>
> それまでの状態: **設計中 (2026-09-30) — Codex 3 回目の 2 件を反映、差分確認待ち**。3 回目の B10 継続・B12 と P3 を Codex が示した形で反映 (往復の上限に達したため、ユーザー決定 2026-09-30: 差分だけを Codex に確認させて確定)。P1 メモリマップ再構築の 2 番目の票 T1 の設計票。ユーザーが T1 の着手を承認 (2026-09-30)。範囲は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) §6 の **T1 の行**で、決定 D1〜D36 は再議論しない。段の分割は T1a〜T1f (§4、**Codex B5 で T1a と T1b の順を入れ替えた**: モデル経路の一本化が先、台帳の核が後)。Codex との突き合わせは 3 回目まで済み (§6-2 / §6-3 / §6-4 に所見と対応)、3 回目の反映の差分確認待ち。コードは未着手。
>
> 発行: コーダー `claude-opus-5-5` (2026-09-30)、PM の指示による。改訂 1: コーダー `claude-fable-5-1` (2026-09-30、Codex `gpt-6-astra` の 1 回目の所見 B1〜B7・X9〜X16・P3 を反映)。改訂 2: 同 (2026-09-30、2 回目の所見 B8〜B11・P3 を反映、§6-3)。改訂 3: コーダー `claude-opus-5-5` (2026-09-30、3 回目の所見 B10 継続・B12・P3 を反映、§6-4)。基点 main `c9a8973` (T0 = C11 は受入完了、[C1] は gnu11)。
> 正典の関係: 決定・帯・規則の正典は [TASK_MEMMAP_V3](TASK_MEMMAP_V3.md) (§0 D1〜D36、§2 帯と lease、§3 台帳と P2V/V2P、§3-5 R1〜R7、§4-5 起動順、§4-7 ローダ契約、§6 T1 の行、§7 受入、§8-4 X4、§10 U1〜U24)。番地の正典は `include/memmap.h`、地図は [`../../02_memory.md`](../../02_memory.md) §2-1 (生成)。この票は **T1 の実装範囲・現状・設計・段・受入**だけを持つ。
> 入力: [U6_PENDING_REVIEW](U6_PENDING_REVIEW.md) §1-4・§1-5、[DEVICE_RESERVATION](../settings/DEVICE_RESERVATION.md) (D33: 核を T1 の台帳へ)、[MEMORY_RAM_INTEGRATION](../../archive/settings/MEMORY_RAM_INTEGRATION.md) (D32: 受入 2 点を T1 へ、撤回済み)、[V3_PLAN_DRAFT](V3_PLAN_DRAFT.md) §3 P1・P4、[APP_BAND_PDE](../memory/APP_BAND_PDE.md)。
> 本文の `file:line` は基点 `c9a8973` の行 (`96decf6` はこの票の追加だけでコードは同じ)。

---

## 1. 範囲

### 1-1. T1 に入るもの (TASK_MEMMAP_V3 §6 T1 の行を漏れなく)

| # | 項目 | 出所 | 段 |
|---|---|---|---|
| 1 | 物理地図 + 所有権台帳 (RAM の存在 / 割当可否 / owner / 写像を別の情報に、PFN 半開区間) | D2、§3-1 | T1b |
| 2 | **SURFACE / lease の型** (記述子の型と台帳の表まで。lease の付け外し API は T2) | §2-2、X3 | T1e |
| 3 | 池を **model 経路で全 RAM 量に** — legacy `pgalloc_init` の fallback を撤去 (8MB を含む) | §2-1、D32 | **T1a** (B5 で先頭へ) |
| 4 | `MEM_POOL_BASE` ほか物理側の定数の分離 (`MEM_PHYS_RAM_CEILING` = 0x80000000 を含む) | D4、D11、§2-3 ⑦ の「物理 (池)」行 | T1a |
| 5 | owner タグ — **AS owner / 永続 owner の 2 種** (R5)、モジュール owner (R7)、device owner (X4) | R5、R7、X4 | T1b |
| 6 | `dma_alloc(size, align, limit)` と DMA プール内の最悪の並び (R4) | D5、R4 | T1c |
| 7 | **集積域・同梱域の予約規則** (定数と台帳初期化の規則。ローダ側の実体は T5b / T6b) | §4-5 | T1a (定数) + T1b (区間の登録) |
| 8 | `sys_reserve_top` の撤去 | D3 | T1e |
| 9 | BB のブート時確保 (owner=boot) と gshell への移譲。**量は識別で決めた backend の量を probe の前に** | §2-2、X7-1 | T1e |
| 10 | **P2V / V2P の導入と物理ポインタ直接参照の監査 + 検査** | D20、§3-4、X7-3 | T1b (台帳の呼び手) + T1f (残り全部と検査) |
| 11 | 割り込み中の確保・解放を**数える**検査 (R1。panic への切り替えは T2) | R1 | T1b |
| 12 | モジュール owner の一括回収 (R7。実モジュールは T4 以降なので API と合成試験まで) | R7 | T1b |
| 13 | DEVICE_RESERVATION の核 (`pgalloc_device_reserve` / `sys_device_reserve_core`) を**台帳の MMIO 登録**に載せ直す (複数 span の一括 commit・永久保持・owner、`test_device_reservation.py` 流用) | D33 | T1d |
| 14 | **MMIO 登録の形 (X4)**: 検証済み資源レコード、予約 / 写像 / 面の範囲の分離、全 span 検査後の一括 commit、永久予約とモジュール回収の分離 | X4、§8-4 | T1d |
| 15 | Cirrus の写像 (`paging_map_phys`) を probe の前へ (識別 → 予約 → 写像 → probe) | D33 | T1e |
| 16 | 受入 (D32): 8MB / 17MB / 64MB のすべてでモデル経路、高位 RAM の登録源は `memory_boot_detect` だけ (U24 の資料照合を含む) | D32、U24 | T1a |

### 1-2. T1 に入れないもの (境界)

| 何 | どこ | T1 との境界 |
|---|---|---|
| アプリ帯 0x80000000 / lease 窓 0xF0000000、共有 USER 写像 (`exec.c:1932-1961`) と共有 PT 経路 (`paging.c:796`) の撤去、lease の付け外し API・検査 3 段・受入 (d) | **T2** | T1 は SURFACE の**型と表**と、BB・窓を SURFACE として登録するところまで。`lease_count` は T1 の間 0 のまま (解放時の検査にだけ使う) |
| `exec_child_claim` / `EXEC_DYN_RESERVE` / `MEM_APP_BAND_DEVICE_FLOOR` / `--cpl0` / `ring3_band_ram_top` の撤去 | **T2** (§6 T2 の行「claim / DEVICE_FLOOR / `--cpl0` 撤去」) | T1 は `sys_reserve_top` だけを消す。CPL=0 の子のための identity アリーナの上端は T1 の間だけ台帳から求める (§3-6) |
| 強制脱出を移譲 / 回収の 2 段に割る (R1 の実装)、R1 の検査を panic に、**`irq_register` / `irq_unregister` の文脈判定を全 IRQ の深さに揃える** | **T2** | T1 は数えて報告するだけ。既存 broker の判定 (`irq_in_irq`) は T1 では触らない (§3-5、B1) |
| `mem_map` / `mem_unmap`、trim 通知 (R2、D23〜D25) | **T2** | T1 の台帳 API は AS owner を明示で受ける形にしておく (§3-2) |
| カーネル帯 3MB 化・KERNEL_SLACK の池への登録・DMA プールを 0x3E0000 (64KB 整列) へ・固定 PT を画像の外へ | **T3** | T1 の DMA プールは今の 0x2E8000 のまま (64KB 境界をまたぐので最大 32KB、`dma_pool.h:31-35`)。`dma_alloc` の口と検査だけ T1 |
| 集積域 0x500000〜 / 同梱域 0x600000〜 の**ローダ側**・`paging_init` / 台帳をルートマウントの前へ (U19) | **T5b / T6b** | T1 は定数と台帳初期化の規則 (bootinfo が同梱を申告したら owner=bundle で予約、集積域は池へ) とホスト試験まで。起動順はいまのまま (ルートマウントが `paging_init` より前、`kernel.c:434` < `:505`) |
| 実モジュールのローダ (§4-7 の検証・公開・IRQ 結び付け)、**PCI BAR の sizing (幅の確定)** | **T4 / T5a / T5c** | T1 は owner=モジュール名 の一括回収と bundle → モジュールの移譲の API と合成試験。PCI は列挙の生値を**採取値**として資源表に取り込むまで (幅未確定 = 予約権限なし、§3-3 ⑥-0) |
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
| device broker の核 | `pgalloc.c:369-471` (`pgalloc_device_reserve`)、`kernel/sys.c:16-31` (`sys_device_reserve_core`) | 最大 16 span、owner (kernel 寿命 ID)、正規化 (同種の重なる span は**併合**、`:404-412`) → 全検査 → 一括 commit、同 owner の完全一致だけ冪等。**`device_boot_map` の RESERVED / MMIO と交差すると拒否** (`:437`)。**呼び手は試験だけ** (`include/sys.h:14`「integration is pending」) | device owner | 永久 | 無 |
| PT workspace | `pgalloc.c:161-190` (`pgalloc_alloc_pt` / `_free_pt`) | model 経路の専用 bitmap `workspace_used` (BSS、16MB 分) | 無し | kernel | 無 |
| PT (master の動的) | `kernel/paging.c:394-421` (`reserve_table`) | model は workspace、legacy は `MEM_APP_BAND_MAX_TOP` より上の恒等 RW ページを 1 枚ずつ | 無し | kernel | 無 |
| AS の PD / PT | `paging.c:706-712` (作成)、`:770-773`・`:925` (解放) | `pgalloc_alloc_page` × (1 + 帯の PDE 数) | 無し (AS 構造体が保持) | AS | **有** (CTRL+STOP・#PF の畳み) |
| アプリの私有ページ | `exec/exec.c:1083-1110` (`app_map_region`)、`:1919-1922` | 連続を試し、だめならページ単位。回収は PTE を辿る (`paging_addrspace_free_user_range`) | 無し (PTE が暗黙の owner) | AS | **有** |
| CPL=0 子の identity アリーナ | `exec.c:939-1000` (`exec_child_claim` / `exec_cpl0_claim` / `_release`) | `[MEM_EXEC_LOAD_ADDR, sys_usable_mem_end())` を A / B に分け `pgalloc_mark_used`、間の `EXEC_DYN_RESERVE` は空ける。**入口の判定は claim より前**: `appslot_cpl0_admit` (`exec/appslot.c:178-184`、呼び手 `exec.c:1775`) が GUI からの起動を `OS32_ERR_INVAL`、生存 CPL=3 アプリがあれば `OS32_ERR_FULL` で断る (試験 20A〜20E、`tools/tests/multiapp_impl_host.c:1615-1627`)。claim 自体は既割当のページに冪等 (owner を見ない)。**今のツリーに `--cpl0` の本体バイナリは無い** (`build/app.conf` に `cpl0` 列は無く、`v86` は `cui` = CPL=3、`:85`) | 無し | CPL=0 の子が居る間 | 無 |
| shlib 帯 | `kernel/shlib.c:95` (`pgalloc_mark_used(MEM_SHLIB_BASE, 256 ページ)`)、失敗経路で `pgalloc_free_n` (`:101`〜`:221`、**11 か所**。`:311` の 1 か所は data 複製の解放) | 固定番地 claim | 無し | kernel (失敗時は返す) | 無 |
| shlib data 複製 | `shlib.c:278` (確保)、`:311` (解放) | `pgalloc_alloc_n(data_pages)` → `kmemcpy((void *)phys, …)` (`:285`) | 無し (attach 表) | AS | **有** (畳みの中の detach) |
| V86 バッキング | `kernel/v86_mem.c:44` (確保 160 ページ連続)、`:165` (解放)。teardown は `v86.c:701,820,878,888,894,922` | `pgalloc_alloc_n(160)` = 640KB **連続** | 無し | V86 セッション | **無** — 脱出は IRQ0 / IRQ1 / 例外スタブから `v86_exit_to_kernel` → `v86_longjmp_out` (`v86.c:502-506`) で longjmp するが、列挙した teardown は `v86_run` の `exec_setjmp` (`v86.c:560`) の**復帰後**に呼ばれる (Codex 1 回目で静的に確認)。件数は §3-5 の計数で 0 を確かめる |
| PEGC BB | `gfx/backend_pegc.c:690-705` (`pegc_reserve_backbuffer`) | `sys_reserve_top(300KB)` (= `pgalloc_reserve_pfn` + `sys_top_reserved`) → 念のため `pgalloc_mark_used` (永久予約済みなので実際は何もしない) → `kmemset((u8 *)s_bb_phys, …)` | 無し (`s_bb_phys`) | kernel | 無 |
| `sys_reserve_top` | `kernel/sys.c:185-214` | 使用可能上限の直下を切り出し、上限 (`sys_frozen_exec` / `sys_top_reserved`) を下げる。唯一の利用者は PEGC BB | 無し | 永久 | 無 |
| planar BB | `include/memmap.h:148-149` (0x6A000、128KB)、`gfx/gfx_core.c:17-20`、`gfx/backend_pc98.c:154` | 低位の固定番地。**静的初期化子** (§2-3 の行を参照、B6) | 無し | 永久 | 無 |
| 低位の固定用途 | `memmap.h:129-149` (フォントキャッシュ 0x1000、Unicode 表 0x4A000、BB 0x6A000、bootinfo 0x7E00)、mailbox 0x90000 | 固定番地。model では `[0, 0x400000)` ごと RESERVED (`physmem.c:101`、`physmem_bootstrap_legacy`) | 無し | 永久 | 無 |
| DMA プール | `memmap.h:288-290` (0x2E8000、64KB)、`kernel/dma_pool.c`、写像は `paging.c:303-304` | 16 ページの bitmap + span 表 (静的)。**0x2F0000 の 64KB 境界をまたぐので 1 回の最大は 32KB** (`dma_pool.h:31-35`)。顧客は PCM リングだけ (`drivers/pcm_cs4231.c:525`)。**82557 のドライバはまだツリーに無い** (PCI 表と文書だけ)。**上下は NP のガード** (下 44KB `0x2DD000`、上 12KB `0x2F8000-0x2FAFFF`、02_memory.md §2-1) | span 表 | 呼び手が free | **有** (設計上 IRQ から呼べる、`dma_pool.c:3-5`。PCM の畳みは CTRL+STOP の IRQ フレームの中、R1) |
| FDC の DMA バッファ | `drivers/fdc.c:54` (BSS の `s_fdbuf`)、`:63,748,851,970` で `(u32)` → 8237 | カーネル BSS を物理として装置へ | — | kernel | 有 (FDC IRQ) |
| デバイス窓 (PEGC) | `backend_pegc.c:546` (`pgalloc_range_has_ram` で 15-16MB に RAM が無いこと)、`:586,600,724` (`paging_map_phys`) | probe の**中で** enable → map → VRAM 書き込み試験 | 無し | kernel | 無 |
| デバイス窓 (Cirrus Xe10) | `gfx/backend_cirrus.c:213` (RAM 無し検査)、`:335` (map)、`:162` (unmap)。glue は `drivers/wab_glue_xe10.c:216-219` (銀行窓 `WAB_XE10_WIN_BASE` 0xF60000 / 32KB、リニア窓 `WAB_XE10_LINEARWIN_BASE` 0xFE000000 / **写像 2MB** `WAB_XE10_LINEARWIN_SIZE`、**decode 4MB** `WAB_XE10_LINEARWIN_DECODE`、`include/wab_xe10.h:173-178` — **どちらも NP21/W の窓の定数で、実機 Xe10 の実測ではない**) | **probe の後に** map (D33 の順に反する)。窓は最初の init で 1 回だけ張る | 無し | kernel | 無 |
| Cirrus の面 (リニア窓の中) | `backend_cirrus.c:65-69` (`CIRRUS_SURFACE_SIZE` 300KB、表示面 `CIRRUS_VIS_OFF` 0、クライアント面 `CIRRUS_CLIENT_OFF` = 300KB)、`:380-381` (`gfx_backend_cirrus.bb_base = s_lin + CIRRUS_CLIENT_OFF` = 0xFE04B000、`bb_size`) | クライアント面は **MMIO の上**。`gfx_bb_phys_range` (`gfx_core.c:143-150`) が `g_backend->bb_base/bb_size` を返し、`exec.c:1959-1962` がアプリ PD で USER に昇格 (`map_user_keep`、PCD を保つ)。表示面は supervisor のまま (契約 G4)。**`bb_base` の読み手はもう 1 つ**: `gfx_core.c:124` (`fb->planes[0]`、CPU 描画の記述子) | 無し | kernel | 無 |
| `setjmp` の保存先 | `include/ksetjmp.h:18` (`KSETJMP_BUF_LEN` 6)、`exec/appslot.h:102` (`AppSlot.jmpbuf[KSETJMP_BUF_LEN]`)、`exec/exec.c:1316` (`g_exit_jmpbuf[KSETJMP_BUF_LEN]`、`:1360` で語数 `KSETJMP_BUF_LEN` の写し)、**`kernel/v86.c:59` (`v86_jmpbuf[6]` — 長さの直書き)**、`kernel/setjmp.asm:8-10` (配置のコメント `buf[6]`)。SDK / userland に写しは無い | 3 か所 (+ 写し 1) | — | kernel | — |
| デバイス窓の PT | `paging.c:239-262` | 0xFE000000〜 の先頭 4MB の PT を静的 1 枚 (`aperture_pt_raw`) | — | kernel | 無 |
| PCI の列挙 | `drivers/pci.c:175-214` (`g_pci[PCI_MAX_DEVS]`、BAR は読むだけ `:206`)。呼び手 `kernel.c:370` (`pci_init`、**`memory_boot_init` `:557` より前**)、結線は `kernel.c:583` (`pci_bind_all`、**`kselftest_run` `:574` より後**) | 生値を BSS の表に持つ。予約も写像もしない | — | kernel | 無 |
| hot-deploy の残骸 | コードは無し (2026-09-09 撤去、`memmap.h:436-446`)。**古い記述だけ残る**: `memmap.h:158`「ホットデプロイ窓の直下を削って取る」、`backend_pegc.c:685`「`sys_hotdeploy_base()` 側」(その関数は無い)、`CLAUDE.md` の Memory layout 表 (`0x8C000` の hot-deploy block — 実際は `V86_TEST_MAGIC_ADDR`、`kernel/v86.h:229` — と top の「Hot-deploy staging」) | — | — | — | — |

### 2-2. 観察 (設計に効くもの)

1. **物理ページに owner が無い。** 今の「所有」は bitmap 1 bit と、呼び手が持つ番地 (`s_bb_phys`、AS 構造体、attach 表、`backing_phys`) と、アプリなら PTE の辿り直しだけ。R5 の「owner=その ID のページが 0」は今は数えられない。
2. **8MB は legacy に落ちる理由が「workspace を 0xC00000 より上に置く」不変条件** (`memory_boot.c:202-209`、[APP_BAND_PDE](../memory/APP_BAND_PDE.md) §2)。2 枚 PDE のアプリが identity のアプリ帯 (0x400000〜0xBFFFFF) を USER で写すと master の PT を書き換えられる、という防御。T2 でアプリ帯が 0x80000000 へ出れば不要になるが、T1 は T2 の前なので**この不変条件を保ったまま 8MB をモデル経路にする**必要がある (§3-3)。**置き場を低位に変えるだけでは通らない** (Codex B2): (a) `physmem_bootstrap_legacy` は `[0, 0x400000)` を RESERVED にし (`physmem.c:101`)、`init_model` (`pgalloc.c:109-121`) は `first < MEM_EXEC_LOAD_ADDR` を拒み、`physmem_reserve_ram` (`physmem.c:156-162`) は全ページが RAM でないと拒む。(b) `sys_memory_bootstrap_model` (`sys.c:94-104`) は「metadata は legacy 上端に接する」「workspace は exec の最小域より上」を要求し、**`sys_frozen_exec = workspace_first * PAGE_SIZE`** で exec の上端を workspace の位置から決める — 低位に置けば exec 上端が 3MB になりロード起点 0x500000 より下 (`sys_usable_mem_end` `sys.c:160-164` の利用者は `exec_child_claim` `exec.c:942` と `ring3_band_ram_top` `:971`)。
3. **`irq_in_irq` (`kernel/irq.c:25`) は共通スタブ `irq_dispatch` (`:132,152`) の中しか数えない。** IRQ0 (タイマ)・IRQ1 (キーボード)・IRQ4・FDC・マウスは専用スタブ (`isr_stub.asm:459,493,548,597,674`) で、深さに入らない。CTRL+STOP の畳み (`ring3_abort_check`、`isr_stub.asm:538`) は IRQ1 スタブの EOI の**後**・`iretd` の前で呼ばれ、longjmp で戻らない。R1 の「割り込みの入れ子深さ」は今の変数では測れない (§3-5)。**しかし `irq_in_irq` は計測専用ではない** (Codex B1): `irq_register` (`irq.c:56`) と `irq_unregister` (`:82`) が `irq_register_check` / `irq_unregister_find` (`irq_math.c:38,74`) に渡し、非ゼロなら `IRQ_ERR_CTX` で断る。PCM 再生中の CTRL+STOP は IRQ1 スタブ → `ring3_abort_check` → `exec_exit` → `exec_reclaim_owned` (`exec.c:1223`) → `pcm_reclaim` → `pcm_release` (`pcm_cs4231.c:460-467`) → `irq_unregister` を**専用スタブの上で**通る。今は `irq_in_irq == 0` なので解除が通るが、この変数を全スタブの深さに置き換えると解除が `IRQ_ERR_CTX` になり、`pcm_release` は戻り値を見ずに `s_irq_reg = 0` にするので登録が残り、次の `pcm_open` (`:532`) が同じ callback の重複登録で `PCM_ERR_BUSY` になる。→ **観測用の深さと broker の判定条件は別の変数にする** (§3-5)。
4. **例外フレームの中でも回収している**: `exception_handler` (`kernel/isr_handlers.c:237-260`) は **#GP 専用ではなく `isr_common` 経由の全ベクタ**を受け、`(CS & 3) == 3 || ring3_in_syscall` なら `ring3_fault_kill` (`:259`) — CPL=3 のゼロ除算 (#DE) も #UD も同じ経路で AS 破棄 → longjmp する (Codex B7)。#PF は `page_fault_handler` (`:347-358`) の同じ形。`ring3_in_syscall` の条件があるので **CPL=0 (syscall の中) の例外も kill 経路に入る**。R1 は ISR を名指しするが、同じ形 (割り込みフレームの上の回収) が例外経路にもある (§5 T1-U3、Codex X12)。例外スタブの入口は `isr_stub.asm` の `isr_common` (`:312`)、`isr_stub_13` の V86 分岐 (`:279-303`)、`isr_stub_14` の両分岐 (`:357-400`)、`ISR_NOERR_V86` / `ISR_ERR_V86` の V86 分岐 (`:157-220`)、`isr_stub_default` (`:410`) — **復帰する経路 (`IRETD_USER` / `iretd`) と longjmp する経路 (`ring3_fault_kill`、`v86_exit_to_kernel`) の両方**がある。
5. **カーネル本体の残り予算は 13.0KB** ([`02_memory.md`](../../02_memory.md) §2-1、583.0KB / 596KB、`__bss_end` 基準でリンク ASSERT が止める)。T1 は T3 (カーネル帯 3MB) より前なので、台帳のコードと kselftest の追加はこの 13KB に入れなければならない (§5 T1-R1)。**BSS もこの予算の内**。
6. **DMA プールは今 64KB 境界をまたぐ** (0x2E8000〜0x2F7FFF)。D5 の「64KB 整列」は置き場の移動 (T3) を待つ。T1 の R4 の試験は「今の池で PCM 16KB + 82557 ≒16KB (`dma_pool.h:4`) が最悪の並びで入る」を見る (両側 32KB ずつなので入る)。**池の上の 12KB `[0x2F8000, 0x2FB000)` は池の上側ガード (NP)** で、その上がカーネルスタックガード `MEM_STACK_GUARD` 0x2FB000 (`memmap.h:90`)。§3-3 で metadata の置き場にするときはガードを 1 ページ残す。
7. **Cirrus のコメントと paging の契約が食い違う** — **静的に決着 (Codex 1 回目、T1-U5 解消)**: `paging_map_phys` (`paging.c:518-530`) は範囲検査 → `prepare_tables` (PT の準備、失敗なら未公開の PT を返して `-1`) → その後にだけ `set_page_noflush` を回すので、**失敗時に PTE は 1 枚も変わらない** (単一呼び出し全件不変)。古いのは `backend_cirrus.c:327-329` の「失敗しても範囲内の分は適用済み」の側で、T1e で map を動かすときにこのコメントと `cirrus_linear_unmap` の「剥がす枚数を控える」前提を直す。DEVICE_RESERVATION §6 の読みは正しい。
8. **`paging_is_present(phys)` に物理番地を渡している**: `exec/exec.c:816,827` (`ring3_pd_range_writable`) は PD / PT の物理番地を `paging_is_present` (仮想を取る) に渡し、`:821,829` で `(const volatile u32 *)pd_phys` と読む。恒等の間は正しいが、P2V の監査で「paging.c の外で表を物理で辿る」唯一の箇所 (T1f で paging.c の関数へ寄せる。**PDE / PTE の USER・RW の判定はそのまま持ち越す**、P3)。
9. **PCI の列挙は台帳より先** (Codex B4): `pci_init` (`kernel.c:370`) は `memory_boot_init` (`:557`) より前で、理由は起動ログの順 (IDE の probe ログより先に `[pci]` を出す)。列挙は `g_pci` に生の BAR を読むだけ (`pci.c:206`)。結線 `pci_bind_all` (`:583`) は `kselftest_run` (`:574`) より後 (自己診断が DMA プールを作り直すため)。→ 資源レコードは列挙時に作れない。**③ の後に `g_pci` から取り込む段を置く** (§3-3 ⑥-0)。
10. **静的初期化子に物理番地のキャストがある** (Codex B6): `gfx/gfx_core.c:17-20` (`bb_b/r/g/i`)、`drivers/kcg.c:52-55` (フォントキャッシュ 4 本)、`lib/utf8.c:32` (`unicode_jis_table`)、`gfx/backend_pc98.c:154` (`gfx_backend_pc98.bb_base`) の **10 か所**は C の静的初期化子で、`static inline` 関数は書けない。マクロ定義の形も 3 か所 (`isr_handlers.c:58-59` `TVRAM_CHAR/ATRP`、`kcg.c:215` `LZ4_TEMP_BUF`、`kapi/kapi_db.c:83` `DB_SHM_PTR` — 最後はリンカ由来の仮想なので対象外)。`uptr` 型は `include/types.h` に無い (`u32` = `unsigned long`、`types.h:20`)。→ §3-4 に定数式で使える形を足す。
11. **今の broker は装置予約を「背景」でも拒む**: `pgalloc_device_reserve` は `device_boot_map` の RESERVED / MMIO と交差すると拒否 (`pgalloc.c:437`)。16MB 超の機では `memory_boot_add_high` (`memory_boot.c:157-165`) が 15〜16MB を RESERVED、`[MEM_PHYS_RAM_CEILING, 4GiB)` を MMIO に**しているので、PEGC の窓 `[0xF00000, 0xF80000)` も Cirrus のリニア窓 0xFE000000 も、この核をそのまま使うと 17MB 以上で予約できない** (今は呼び手が試験だけなので顕在化していない)。→ 区間の表で「RAM でない背景」と「固定用途」と「装置の予約」を分ける (§3-1、Codex P3)。
12. **`GFX=` の読み取りの依存**: `sysconfig_get_str` (`kernel/sysconfig.c:118-130`) は `sc_load` → `vfs_read` (`:39`) だけに依存し、`exec_init` に依存しない。ルートは `kernel.c:434` でマウント済みなので ⑥ を `exec_init` の前に置ける (§5 T1-R4 は静的に解消)。
13. **`setjmp` の保存先は 3 か所で、1 つは長さを直書きしている** (Codex B8): `AppSlot.jmpbuf` (`appslot.h:102`) と `g_exit_jmpbuf` (`exec.c:1316`) は `KSETJMP_BUF_LEN` を使うが、**`v86_jmpbuf` (`v86.c:59`) は `[6]` の直書き**。`exec_setjmp` の保存形式を広げると、`v86 -t` → `v86_run_limit` (`v86.c:517`) → `exec_setjmp(v86_jmpbuf)` (`:560`) が保存先を越えて書く。→ 保存形式を広げる段で **3 か所すべてを `KSETJMP_BUF_LEN` に統一**する (§3-5)。
14. **選択中 backend の面の情報源は `g_backend->bb_base/bb_size`** で、読み手は `gfx_bb_phys_range` (`gfx_core.c:143-150`、呼び手は `exec.c:1959` の 1 か所) と `fb->planes[0]` (`gfx_core.c:124`) の 2 つ (Codex B9)。Cirrus では `bb_base` が**リニア窓の中のクライアント面** (MMIO、0xFE04B000、300KB) で、これを SURFACE 表に載せないまま `gfx_bb_phys_range` を SURFACE 参照にすると、`cirrus-on` の GUI アプリが USER 昇格を失って最初の描画で #PF になる。→ Cirrus の面も T1e で SURFACE に登録する (§3-8)。
15. **IRQ7 の専用スタブ (`isr_stub.asm:567-590`) は `RESTORE_KSEG` を行わず、`push eax` だけで PIC を読んで `IRETD_USER` する** (Codex P3)。CPL=3 や V86 から入ると DS は利用者側の値のままなので、無修飾のメモリ参照は使えない。他の専用スタブ (`:461,495,550,599,642,657,676`) と共通スタブ (`:437`)、例外スタブ (`:172,206,282,314,365,379`) は `pushad` の後に `RESTORE_KSEG` を行う。→ 深さの更新は **`ss:` セグメントで行う** (`RESTORE_KSEG` の前後に依存しない、§3-5)。SS の出所は 2 通り (Codex 3 回目 P3 で訂正): V86 / CPL=3 からの入口は特権遷移で TSS の `ss0` = `KERNEL_DS` 0x10 (`kernel/tss.c:36`) が載る。CPL=0 からの入口は TSS を通らず既存の SS を保つが、それは GDT ロード時に設定した flat SS 0x10 (`arch/x86/x86_desc.h:24` の `x86_load_gdt`、SS の再ロードは `:34`) なので同じ番地を指す。V86 入口で null 化されるのは DS 等で、SS はゲストの値ではない。
16. **複数の資源を 1 回で予約する要求は gfx の候補群だけではない**: Ra266 の列挙 (TASK_LAN_82557 §6 R1、2026-09-23) では **Trident 9660 (`0:8.0`) は memory BAR 3 本** (0x20000000 / 0x20400000 / 0x20800000) + Expansion ROM、**82557 (`0:11.0`) は memory BAR 2 本** (0x20410000 CSR、0x20500000 flash) + I/O BAR。装置 1 つの予約が複数の資源レコードにまたがる (Codex B10 の一般形)。PCM CS4231 は I/O だけ (`drivers/pcm_cs4231.h:27-31`) で台帳の顧客ではない。NEC display `0:7.0` は BAR 全部 0、CA0106 は I/O だけ → Ra266 のメモリ BAR は 5 本 + ROM 1 本。**装置 1 つの中の離れた aperture も別レコード** (Codex 3 回目 B10: 資源レコードの decode 区間は 1 組なので、Xe10 の銀行窓とリニア窓は Xe10-bank / Xe10-linear の 2 本)。gfx の候補群は PEGC・Xe10-bank・Xe10-linear の 3 本。**見積り: gfx 3 本 + Ra266 の PCI 採取値 6 本 = 9 本** で資源の表 16 本に入る (NP21/W は PCI absent なので gfx の 3 本だけ)。
17. **T1b〜T1d の間、BB は今の `sys_reserve_top` 経路のまま**なので、`pegc_reserve_backbuffer` (`backend_pegc.c:687-698`) は `sys_reserve_top` → `pgalloc_reserve_pfn` (永久予約) の後に `pgalloc_mark_used` を呼ぶ (Codex B11)。永久予約の L2 の owner を kernel にしたまま boot で claim すれば「他 owner 混在」で必ず拒否される。→ 暫定経路に owner を通す (§3-2、§4-8)。

### 2-3. 物理ポインタの直接参照 — 監査の見積り (P2V / V2P の導入対象)

`kernel/ drivers/ gfx/ fs/ exec/ kapi/ lib/` を走査 (third_party・SQLite・fatfs・zlib・microtar・os32_lz4・userland は除く)。grep と目視で ±数件。**Codex の再集計では書き換え行の合計は 161 件** (下の表の「約」を訂正)。**全件を監査した証拠ではない** — T1f で 1 件ずつ分類し、`check_p2v.py` の例外一覧に理由付きで残す。

| 分類 | 向き | 件数 | 主な場所 |
|---|---|---:|---|
| VRAM / TVRAM / PEGC / 窓の定数キャスト | P2V | 59 | `console.c` 15、`gfx_core.c` 18、`gfx_vram.c` 8、`backend_pegc.c` 7、`v86_gcap.c` 4、`kernel.c` 2、`ime.c` 2、`isr_handlers.c` 2 (マクロ `:58-59`)、`backend_cirrus.c:345` 1 |
| BDA / IVT (0x000〜0x5FF) | P2V | 6 (+ V86 のページ 0 経由 4) | `sysclk.c:29`、`kbd.c:264`、`backend_pegc.c:134` (`bios_flag`、呼び手 5)、`memory_boot.c:79`、`v86_bios.c:265,278` |
| `MEM_*` 固定番地のキャスト | P2V | 16 | `kernel.c:608`、`bootinfo.c:59`、`kselftest.c:790`、`gfx_core.c:17-20`、`backend_pc98.c:154`、`lib/utf8.c:32`、`kcg.c:52-55,215`、`shlib.c:77` |
| うち **静的初期化子・マクロ定義** (関数の中でない) | P2V (定数式) | 10 + 3 | §2-2 の 10. — `P2V_CONST` の対象 (§3-4、B6) |
| PD / PT を物理で触る | P2V | 21 (+`paging_is_present(phys)` 2) | `paging.c` 19 (`:400,418,441,453,717,737,794,799,836,889,892,922,1022,1083,1099,1100,1111,1129,1134`)、`exec.c:821,829` |
| PFN × PAGE_SIZE → ポインタ (恒等の検証・起動前の検出) | P2V | 5 + 4 | `pgalloc.c:126,170,211,219`、`memory_boot.c:217`、`memory_boot.c:79,89,90,104` (`boot_ptr` — P2V の種になる既存の唯一の関数、`memory_boot.c:26`) |
| 物理確保の結果 → ポインタ | P2V | 7 | `backend_pegc.c:704,705,847,932`、`shlib.c:210,285`、`dma_pool.c:46` |
| ポインタ → PDE / CR3 / 恒等比較 | V2P | 10 | `paging.c:253,262,331,386,413,442,457,626,1097`、`pgalloc.c:103,114` |
| カーネル静的ポインタを PTE の物理に | V2P | 3 | `exec.c:158,1964` (トランポリン)、`gfx_core.c:148` (`bb_base`) |
| DMA へ渡すポインタ | V2P | 6 | `fdc.c:63,748,851,970` (BSS `s_fdbuf`)、`dma_pool.c:56,68` |
| HostDrv の hypercall に渡すポインタ | V2P (要確認) | 24 | `fs/hostdrvfs.c:107,144-381` — NP21/W が線形番地と物理番地のどちらで読むかはコードに書かれていない (§5 T1-U6) |
| 恒等写像の呼び出し (virt と phys に同じ値) | 見直し | 33 | `paging.c`、`shm.c`、`shlib.c`、`v86_mem.c`、`v86_bios.c`、`pgalloc.c:217`、`backend_pegc.c`、`backend_cirrus.c`、`exec.c:1352-1964` |
| **仮想なので変えないもの** | — | ≈35 | V86 のゲスト線形 (`v86.c:98 v86_ptr` ほか)、リンカ由来 (`KHEAP_BASE`、`MEM_SHM_BASE`、トランポリン)、ユーザ / シェル帯、ポインタ算術 |

**合計: P2V / V2P の書き換え 161 件 (Codex 再集計)、見直し ≈ 45 件、対象外 ≈ 35 件。** P2V / V2P という名の既存関数は無い。近いもの: `boot_ptr` (`memory_boot.c:26`)、`mmio_w8/16`・`bios_flag` (`backend_pegc.c:105,111,127`)、`dma_pool_alloc` の `phys_out`、`paging_verify_identity` (`paging.c:372`、P = V を表明する)。

---

## 3. 設計

### 3-1. 台帳のデータ構造

台帳は 4 層。**既存の `physmem` / `pgalloc` の表をそのまま下の 2 層に使い**、上に owner と区間の表を足す (新しい allocator は作らない)。

| 層 | 持つもの | 実体 | 新規か |
|---|---|---|---|
| L0 物理地図 | RAM の存在と種別 (RAM / RESERVED / MMIO / UNKNOWN)、source (LEGACY / MACHINE) | `struct physmem` (`physmem.h`、1,032B)、起動時に 1 回作って凍結。`pgalloc` の私有の写し `device_boot_map` (`pgalloc.c:25`、BSS。**T1 では動かさない**) | 流用 |
| L1 割当可否 | eligible bitmap / allocated bitmap (PFN ごと 1 bit ずつ) | `pgalloc.c` の 2 bitmap (metadata) | 流用 |
| L2 **owner** | PFN ごとの owner 番号 (`u8`、0 = 空き)。**不変条件: allocated ⇔ owner ≠ 0** (bitmap と owner 配列を**同じ IRQ 保存区間で更新し、途中で失敗したら両方戻す**、X11。workspace の PT は L2 では owner = kernel、**永久予約 (`pgalloc_reserve_pfn` の後継) は呼び手が渡す owner** — T1b〜T1d の暫定 `sys_reserve_top(owner, bytes)` は boot (B11、§4-8)。bitmap は「eligible を落とす」— 不変条件の対象は eligible なページだけ。**同じ owner のページへの `ledger_claim_fixed` は冪等**) | 新設。metadata の同じ塊から切り出す (`pgalloc_metadata_bytes` を 2 bit + 8 bit / PFN に) | 新規 |
| L3 **区間の表** | PFN 半開区間ごとの種別・owner・キャッシュ属性・資源レコード。bitmap の管理範囲外 (MMIO、4GiB 端) も完全な範囲で持つ | 新設。固定長の配列 (動的確保なし、**BSS**) | 新規 (`device_ledger` の後継) |

**owner の表** (`struct ledger_owner`、固定長 64 本 = owner 番号 1〜63。番号 0 は空き)

| 項 | 内容 |
|---|---|
| `kind` | **AS** (アプリの ID。終了で全部回収、R5 のゼロ検査の対象) / **PERSIST** (kernel・boot・bundle・shlib・gshell。AS の終了経路は触らない、R5 (b)) / **MODULE** (モジュール名。永続だが R7 の一括回収の単位) / **DEVICE** (kernel 寿命の安定 ID。MMIO の永久予約だけを持ち、**RAM の回収 API はこの種別を拒否**する — X4 の「永久予約とモジュール回収の分離」。**返却もしない**) |
| `id` | AS なら app ID (`appslot`)、他は固定の列挙 |
| `name[8]` | `mem` の表示用 |
| `pages` | 持っている RAM のページ数 (L2 と一致することを kselftest が見る) — **会計** (失敗時は不変) |
| `alloc_irq`, `free_irq` | R1 の件数 (§3-5) — **診断** (失敗した操作でも増えてよい、§3-2) |

固定の番号: 1 = kernel、2 = boot、3 = bundle、4 = shlib、5 = gshell、6 = staging (集積域)、7 = gfx (DEVICE、§3-8)。AS と MODULE と他の DEVICE は必要時に割り当てる。

**owner 番号の寿命** (Codex P3): AS owner は `exec` が AS 作成時に取り、AS 破棄で `pages == 0` を確かめてから `ledger_owner_retire` で返す。**返却の条件**は「`pages == 0` かつ L3 のどの区間も SURFACE もその番号を参照していない」(参照が残れば拒否して `retire_refused` を数える — 移譲し忘れの検出)。返した番号は**再利用する** (最大 63 本なので回す。古い参照は上の条件で残らない)。PERSIST は返さない。MODULE は R7 の一括回収 (`ledger_reclaim_owner`) の後に返す。DEVICE は返さない。

**区間の表** (`struct ledger_region`、固定長 48 本)

| 項 | 内容 |
|---|---|
| `first`, `end` | PFN 半開区間 (`end` = 1048576 で 4GiB 端を表せる。バイトの排他端は使わない — 既存 `physmem` と同じ) |
| `type` | FIXED (カーネル帯・SQLite 帯・シェル帯・低位の固定用途・固定 PT・カーネルスタック・台帳の backing)、**BACKGROUND** (RAM でない背景: 15〜16MB のシステム空間 `[MEM_SYSTEM_SPACE_BASE, MEM_HIGH_RAM_BASE)`、`[MEM_PHYS_RAM_CEILING, 4GiB)`。**装置の予約はここに重なってよい** — 予約は背景を細分するもので、背景そのものは装置ではない、Codex P3)、STAGING (集積域)、BUNDLE (同梱域)、DMA (DMA プール)、**DEVICE** (装置の予約 = `ledger_reserve_set` が作る。MMIO でも RAM でも)、SURFACE_BACKING (SURFACE が参照する固定 RAM、planar BB) |
| `owner` | owner 番号 |
| `cache` | WB / UC (MMIO と Cirrus の面は UC = PCD。lease の PTE はここから引く、§2-2「キャッシュ属性は台帳から」) |
| `flags` | PERMANENT (解除 API なし)、OUTSIDE (bitmap の管理範囲 `[0, limit_pfn)` の外にかかる) |
| `res_mask` | **根拠になった資源レコードの集合** (`u16`、資源の表 16 本のビット。DEVICE だけ。正規化で複数の span が 1 区間に併合されても、それぞれの根拠が残る — Codex B10) |
| `span_set` | 同じ一括 commit で入った区間の組の番号 (同 owner の完全一致判定に使う — `pgalloc_device_reserve` の冪等規則) |

**拒否規則** (`ledger_reserve_set`、`pgalloc_device_reserve` の `device_boot_map` 照合 `pgalloc.c:437` を置き換える): 要求の span は (a) **FIXED / STAGING / BUNDLE / DMA / SURFACE_BACKING と交差したら拒否**、(b) **他 owner の DEVICE と交差したら拒否**、(c) BACKGROUND との交差は許す、(d) RAM (L1 で eligible) の部分は allocated でないことと、`SYS_DEVICE_RAM` の要求なら全ページ eligible であることを見る (今の `:449-455` のまま)、(e) **span ごとに指定した資源レコード `res` の decode 範囲の内側であること、その `width_basis` が RAW / PROBE_UNVERIFIED でないこと** (X4 の予約権限。正規化の**前**に span 単位で照合する — Codex B10)。正規化で併合した区間は `res_mask` に根拠を全部残す。同 owner・同一集合 (正規化後の区間集合と `res_mask` が一致) は冪等、同 owner の部分一致は拒否 (今の `:421-431`)。

**検証済み資源レコード** (`struct ledger_resource`、固定長 16 本、X4)

| 項 | 内容 |
|---|---|
| `bus` | PCI / CBUS (C バスの窓) / FIXED (機種固定、PEGC) |
| `bdf`, `vendor`, `device`, `revision`, `bar`, `raw_bar` | PCI のとき (列挙の読み取り値。`drivers/pci.c:206` は BAR を読むだけで書かない) |
| `decode_first`, `decode_end` | **予約範囲** = 装置が応答する decode aperture 全体 (PFN) |
| `map_first`, `map_end` | **写像範囲** = 確認済みの必要部分 (`paging_map_phys` で sup+PCD に張る範囲)。例: Xe10 のリニア窓は decode 4MB (`WAB_XE10_LINEARWIN_DECODE`) に対し写像 2MB (`WAB_XE10_LINEARWIN_SIZE`) |
| `width_basis` | decode 幅の根拠: SIZING (BAR の sizing を実施 — T4 以降)、DATASHEET (資料)、GLUE_CONST (glue の定数、例 Xe10 — **NP21/W の窓の値で実機の実測ではない**ことをレコードの `name` / コメントに残す)、**RAW (列挙の採取値。幅未確定 = 予約権限にしない)**、PROBE_UNVERIFIED (未検証 — 予約権限にしない) |
| `boot_gen` | 起動世代 (GUI 境界での同一性の再確認に使う — 再確認そのものは P4) |

**1 レコード = decode 区間 1 組** (Codex 3 回目 B10): 装置が離れた aperture を複数持つときは **aperture ごとに別レコード**にする。Xe10 は銀行窓 `[0xF60000, 0xF68000)` の **Xe10-bank** とリニア窓 `[0xFE000000, 0xFE400000)` の **Xe10-linear** の 2 本、PCI は BAR ごとに 1 本 (Trident 3 本 + ROM 1 本、82557 2 本)。両窓を包む 1 つの巨大な decode 区間にはしない (間の未確認の隙間を検証済み decode として扱うことになり、X4 を満たさない)。`res_mask` は照合後の根拠を残すだけで、この分割の代わりにはならない。

欄の並びは **u32 → u16 → u8 の順** (`raw_bar`、`decode_first/end`、`map_first/end`、`bdf`、`vendor`、`device`、`bus`、`revision`、`bar`、`width_basis`、`boot_gen`) にして詰め物を出さない — 表の記載順のままだと自然配置で 36B になる (Codex P3)。`STATIC_ASSERT(sizeof(struct ledger_resource) == 32)`。

面範囲 (USER に lease する面) は SURFACE の側に持つ (下)。**3 つの範囲は別の表・別の欄** — 同じ `{base, size}` を予約権限にも写像にも lease にも使わない (X4)。

**SURFACE** (`struct ledger_surface`、固定長 8 本。T1 は型と表と登録だけ、lease の付け外しは T2)

| 項 | 内容 |
|---|---|
| `owner` | boot → gshell (BB)、kernel (TVRAM・表示面) |
| `backing` | RAM (池から確保した PFN 区間) / FIXED_RAM (planar BB 0x6A000) / VRAM (0xA0000〜) / MMIO (PEGC リニア窓、**Cirrus のクライアント面と表示面** — B9) |
| `backend`, `role` | どの backend の面か (PC98 / PEGC / CIRRUS)、役 (CLIENT = アプリへ USER で貸す面、DISPLAY = 表示面、supervisor のまま)。`gfx_bb_phys_range` は「選択中 backend の CLIENT」を返す (§3-8) |
| `first`, `npages` | **ページ単位で占有** (面の末尾をページへ切り上げ、同じページを他の用途と共有しない、§2-2) |
| `width`, `height`, `pitch`, `format`, `planes` | 面の形 (planar は 4 プレーン) |
| `cache`, `perm_max` | 区間の表から引いた属性、貸せる権限の上限 |
| `gen` | 世代 (再 init・モード変更で面の意味を変えるときに上げる。既存 lease があれば先に revoke — T2) |
| `lease_count` | T1 の間は常に 0。**物理の解放は `lease_count == 0` かつ owner が返したときだけ** |

**移譲・回収で L2 / L3 のどちらを更新するか** (Codex P3):

| 操作 | L2 (ページの owner) | L3 (区間・SURFACE) |
|---|---|---|
| `ledger_transfer` (bundle → モジュール、RAM 版 BB boot → gshell) | 全ページを `to` へ (会計 `pages` も動く) | BUNDLE 区間は type そのまま・owner=bundle のまま (区間は「同梱域の場所」であって中身の owner ではない)。RAM backing の SURFACE は `owner` を `to` へ |
| `ledger_surface_transfer` (planar の固定 SURFACE boot → gshell) | 触らない (固定 RAM は eligible でない) | SURFACE_BACKING 区間の owner と SURFACE の owner を両方 `to` へ |
| `ledger_surface_transfer` (**MMIO backing の SURFACE** — Cirrus CLIENT boot → gshell、Codex 3 回目 P3) | 触らない (MMIO は RAM でない) | **SURFACE の owner だけ**を `to` へ。その面を含む DEVICE 区間 (`[0xFE000000, 0xFE400000)`) の owner = `gfx` は変えない (装置の予約は候補群のもの)。同じ窓の中の DISPLAY の SURFACE (owner = kernel) も変えない |
| `ledger_reclaim_owner(MODULE)` | その owner の全ページを解放 | その owner の区間は無い (MODULE は MMIO を持たない — MMIO は DEVICE)。DEVICE の区間・写像は無傷 |
| `ledger_reclaim_owner(AS)` | 全ページ解放 (取り残しの件数を診断へ) | SURFACE を持っていたら拒否 (T1 では AS は SURFACE を持たない) |
| `ledger_reclaim_owner(DEVICE)` / `ledger_reclaim_owner(PERSIST)` | 拒否 | 拒否 |

**大きさ (U1 の一部、Codex P3 で実サイズに)**: 欄を素直に並べた見積り — owner 24B (`kind` 1 + `id` 1 + `name` 8 + `pages` 4 + `alloc_irq` 4 + `free_irq` 4 = 22 → 24) × 64 = **1,536B**、区間 16B (4+4+1+1+1+1+1+1 = 14 → 16) × 48 = **768B**、資源 32B (1+2+2+2+1+1+4+8+8+1+1 = 31 → 32) × 16 = **512B**、SURFACE 24B (1+1+4+4+2+2+2+1+1+1+1+1+1 = 22 → 24) × 8 = **192B**。固定表の合計 **≈ 3.0KB、置き場は BSS** (大きさが RAM 量によらないので metadata に入れる理由がなく、8MB 型の置き場 (§3-3) を小さく保てる)。`device_boot_map` 1,032B は BSS のまま動かさない。**BSS の差し引き: +3.0KB (固定表) − 1KB (`legacy_metadata`、T1a で撤去) ≈ +2KB** (§5 T1-R1 の予算 13.0KB から引く)。L2 は 1B / PFN、L1 と合わせて **1.25B / PFN** (metadata): 8MB 2,560B、12MB 3,840B、17MB (4,352 PFN) 5,440B、64MB 20,480B、2GB 655,360B。実測は T1b の受入で報告する。`STATIC_ASSERT` で固定表の合計と、8MB 型の metadata が 1 ページに入ること (§3-3) を止める。

### 3-2. API (カーネル内部だけ。KAPI は変えない)

**原則** (既存の `pgalloc` / `physmem` の流儀をそのまま): 物理は **PFN (u32) で返し、成功は別の戻り値** (PFN 0 と最後のページを表せる)。全 mutator は **全部検査してから commit、commit の後に失敗しうる処理を置かない**、失敗時は台帳 (L1・L2・L3)・**会計** (owner の `pages`、`total_pages` / `used_pages`、区間の本数)・出力引数を全部不変。IF は `irq_save` / `irq_restore` で保存。**owner は必ず引数で渡す** — 暗黙の `res_owner_get()` を使わない (§3-5-3 の 2.「owner の安定」の下準備)。

**「統計」の定義** (Codex P3): 失敗時全不変の対象は**会計**。**診断カウンタ** (`bad_free`、`retire_refused`、`claim_refused`、`res_overflow`、`ledger_irq_ops` / `ledger_exc_ops`、owner の `alloc_irq` / `free_irq`) は失敗した操作でも増える (それが役目)。kselftest の不変条件は会計だけを見る。

| API (名は案、T1b で確定) | 意味 | 置き換える今の口 |
|---|---|---|
| `ledger_owner_new(kind, id, name, *owner)` / `ledger_owner_retire(owner)` | owner 番号の取得 / 返却 (§3-1 の寿命の規則) | — |
| `pgalloc_alloc_n_owner(owner, n, first, end, flags, *pfn)` | n ページ連続 (flags: TOP_DOWN / BOTTOM_UP)。`alloc_n_pfn` (`pgalloc.c:248`) に owner の書き込みを足す。`[first, end)` は探索範囲 (§3-6 の BB は `[MEM_EXEC_LOAD_ADDR / PAGE_SIZE, pgalloc_arena_end())`) | `pgalloc_alloc_n` / `_page` / `_n_range` / `_n_pfn` |
| `pgalloc_free_n_owner(owner, pfn, n)` | **全ページが owner のものであるときだけ**解放 (他 owner のページが混じれば全件不変で拒否し `bad_free` を数える) | `pgalloc_free_n` / `_page` / `_n_pfn` |
| `ledger_transfer(pfn, n, from, to)` | 全ページが `from` のものなら一括で `to` へ (bundle → モジュール、boot → gshell) | — |
| `ledger_reclaim_owner(owner, *pages)` | owner のページを全部返す (R5 の AS 終了、R7 のモジュール失敗)。**DEVICE / PERSIST owner、`lease_count > 0` の SURFACE を持つ owner は拒否** | — (今は PTE を辿るだけ) |
| `ledger_claim_fixed(owner, first, end)` | 固定番地を owner で押さえる (shlib 帯、CPL=0 子のアリーナ)。**他 owner のページが 1 つでも混じれば全件不変で拒否** (`claim_refused`) | `pgalloc_mark_used` |
| `ledger_register_region(type, owner, first, end, cache, flags)` | 起動時 (live AS = 0) だけ。FIXED / BACKGROUND / STAGING / BUNDLE / DMA / SURFACE_BACKING の登録 | — |
| `ledger_reserve_set(owner, const struct ledger_span spans[], n)` — `ledger_span = {first, end, kind, res}` | **MMIO / 永久 RAM の一括予約** — `pgalloc_device_reserve` の核を流用 (**span ごとの `res` を decode 範囲と `width_basis` で照合** → 正規化 (同種の重なる span は併合、`pgalloc.c:404-412`、併合した区間の `res_mask` は和) → §3-1 の拒否規則で全件照合 → 一括 commit、同 owner の完全一致だけ冪等、管理範囲外も完全な範囲を記録し bitmap 操作は交差だけ)。1 回の要求で複数の資源にまたがれる (gfx の候補群 3 窓、Trident の BAR 3 本、82557 の BAR 2 本 — B10) | `pgalloc_device_reserve` / `sys_device_reserve_core` |
| `sys_reserve_top(owner, bytes)` (**T1b〜T1d の暫定**) | 今の `sys_reserve_top` に owner を足したもの。永久予約したページの L2 を `owner` にする (PEGC BB は boot)。T1e で撤去 (B11、§4-8) | `sys_reserve_top(bytes)` |
| `v86_mem_setup(owner, …)` | V86 バッキングの確保に owner を渡す (KAPI 経由なら呼び手の AS、カーネルからなら kernel) | `v86_mem_setup` |
| `ledger_resource_add(rec, *rid)` / `ledger_resource_import_pci()` | 資源レコードの登録 / `g_pci` の生 BAR (メモリ種別で非 0 のもの) を `width_basis = RAW` で取り込む (§3-3 ⑥-0)。表が溢れたら残りを捨てて `res_overflow` を数える | — |
| `ledger_surface_create(owner, backing, first, npages, geom, *sid)` / `ledger_surface_transfer(sid, to)` | SURFACE の登録と移譲 | — |
| `ledger_owner_pages(owner)`、`ledger_dump(cb)`、`ledger_selfcheck(tag)` | `mem` と kselftest。`ledger_selfcheck` は不変条件 (allocated ⇔ owner ≠ 0、`pages` の合計、区間の非重複、SURFACE の参照先が生きている) を**任意の時点**で検査し、失敗を `ledger_check_fail` (件数) と `ledger_check_tag` (最後に失敗した地点) に残す (§3-3 の検査地点) | `pgalloc_free_pages` は残す |
| `pgalloc_arena_end()` | **legacy アリーナ (低位の連続 RAM) の上端 PFN** — model の初期化で予約を差し引いた後の `physmem_legacy_end` (§3-3、B2)。T1a で追加 | `sys_frozen_exec` の算出元 |
| `ledger_arena_top()` | **T1 の間だけ**: CPL=0 子の identity アリーナの上端 (§3-6)。T2 で消す | `sys_usable_mem_end` の `sys_top_reserved` 部分 |

呼び手の書き換え (T1b): `paging.c` の AS の PD / PT (owner = その AS) と `reserve_table` (kernel)、`paging_app_band_selftest` (試験用 AS の owner を明示的に取り、2 ページもその owner で確保・解放 — §4-8、B12)、`exec.c` の `app_map_region` と teardown (AS、最後に `ledger_reclaim_owner` で取り残しを掃除して件数を診断に出す)、`shlib.c` の帯 (shlib) と data 複製 (AS)、`v86_mem.c` のバッキング (V86 セッションを起動したアプリの AS)、`backend_pegc.c` の BB (boot、T1e で SURFACE へ)、`exec.c` の CPL=0 claim (§3-6)。**owner を取らない旧 API は T1b の最後で消す** — 取り残しをリンクエラーで見つける。

### 3-3. 起動順 — 台帳をいつ作り、誰が何を登録するか

T1 は起動順を大きく変えない (ルートマウントを `paging_init` の前に置いたまま。U19 と 2 段階の起動順は T5b)。変わるのは ③ の中身と ⑥-0 / ⑥ の新設だけ。

| # | 段 (今の位置) | T1 での中身 | live AS |
|---|---|---|---|
| ⓪ | `pci_init` (`kernel.c:370`、`paging_init` より前) | 変更なし (列挙は `g_pci` に生値を読むだけ)。台帳にはまだ書けない (B4) | 0 |
| ① | `memory_boot_detect` (`kernel.c:191`、`paging_init` の前) | 変更なし。**高位 RAM の登録源はここだけ** — `physmem_add_trusted(MACHINE)` の呼び手が `memory_boot.c` だけであることをホスト検査で見る (D32) | 0 |
| ② | `paging_init` (`kernel.c:505`) | 8MB 型の backing `[0x2F9000, 0x2FB000)` を present / supervisor / RW にする (今は NP の「カーネル予約 (上)」。**0x2F8000 の 1 ページはガードとして NP のまま**)。他は変更なし | 0 |
| ③ | `memory_boot_init` (`kernel.c:557`) → **台帳の初期化** | 地図 (L0) → metadata の置き場を決める (下、**backing の種類で検証経路が違う**) → L1/L2 → `pgalloc_arena_end()` を凍結し `sys_frozen_exec` に (B2) → 区間の表に固定用途を登録: 低位 (NULL ガード・フォント・Unicode・planar BB (SURFACE_BACKING)・bootinfo・mailbox・VRAM・ROM)、カーネル帯、SQLite 帯、DMA プール、台帳 backing (8MB 型)、固定 PT、カーネルスタック、シェル帯、15〜16MB (**BACKGROUND**)、`[MEM_PHYS_RAM_CEILING, 4GiB)` (**BACKGROUND**)。**集積域・同梱域の規則**: bootinfo が同梱エントリを申告していれば `MEM_BOOT_BUNDLE_*` を owner=bundle で予約、`MEM_BOOT_STAGING_*` は展開済みなので池へ (T1 の時点ではローダが使わないので両方とも空振り — 規則とホスト試験だけ) → `stage_online` | 0 |
| ④ | `dma_pool_init` (`kernel.c:567`) | 池は台帳の DMA 区間の上。`dma_alloc` の口 (§3-7)。**kselftest の `test_dma_pool` が末尾で `dma_pool_init()` を呼び直す** (`kselftest.c:915`) が、それは span 表の作り直しで台帳の DMA 区間には触らない | 0 |
| ⑤ | `kselftest_run` (`kernel.c:574`) | `ledger_selfcheck("boot")` + R4 の最悪の並び。**ここは ⑥ より前**なので、gfx の登録と GUI 移譲の検査は下の地点で別に取る (Codex P3) | 0 |
| ⑥-0 | **新設: PCI 採取値の取り込み** (`pci_bind_all` `kernel.c:583` の直前) | `ledger_resource_import_pci()` — `g_pci` のメモリ BAR を `width_basis = RAW` で資源表へ (採取値。**予約権限にならない**)。検証済みレコード (SIZING / DATASHEET / GLUE_CONST) は driver か glue が別に登録する。NP21/W (PCI absent) では 0 本、Ra266 では `[pci]` の行の本数と一致することを受入で見る | 0 |
| ⑥ | **新設: gfx の識別 + MMIO 登録 + 写像 + BB 確保** (`exec_init` の前、`kernel.c:597` の手前) | `GFX=` の読み取り (今は `kernel.c:673-690`、`exec_init` の後) を**ここへ前倒し** (依存は `vfs_read` だけ、§2-2 の 12.) → 副作用のない識別 (§3-8) → 候補の窓を **1 回の** `ledger_reserve_set` (owner = DEVICE `gfx`、§3-8) → `paging_map_phys` (sup+PCD、写像範囲だけ、全戻り値を検査) → BB を池から連続確保 (owner = boot、SURFACE、探索範囲は §3-6) → `ledger_selfcheck("gfx")` | 0 |
| ⑦ | `exec_init` → … → `boot_splash` → `gfx_prepare_backend` (`kernel.c:696,707`) | probe / enable は今の位置のまま (GUI 境界への移動は P4)。**probe は ⑥ で張った写像と確保済みの BB を使い、自分では予約も写像もしない** | 0 |
| ⑧ | `shlib_init` (`kernel.c:712`) | 帯を `ledger_claim_fixed(shlib)`、失敗経路は `pgalloc_free_n_owner(shlib, …)` (11 か所)。**固定 shlib 帯を ③ で永久除外しない** — claim / 失敗時返却の今の形を保つ (X9) | 0 |
| ⑨ | GUI の開始 (`kernel.c:785` の `is_gui` の分岐、`exec_run(gshell)` の直前) | `ledger_surface_transfer(BB, gshell)` (RAM 版は `ledger_transfer` も) → `ledger_selfcheck("gui")`。2 回目以降 (GUI → CUI → GUI) は同じ owner なので無操作 — BB は返さず保持 (R5 (b)) | 0 |

**metadata (L1 + L2) の置き場** (legacy fallback の撤去、D32。固定表は BSS なので置き場には入らない):

| 構成 | 置き場 (backing の種類) | 検証経路 | exec 上端 |
|---|---|---|---|
| 低位 RAM の末尾に `metadata + workspace` が入り、下端が `MEM_APP_BAND_MAX_TOP` (0xC00000) 以上 (低位 RAM の上端が **3,074 PFN 以上** ≒ 12MB + 8KB。17MB・64MB) | **ARENA_TOP** — 今と同じ (低位 RAM の末尾、`memory_boot.c:216-219`) | 今と同じ: `physmem_reserve_ram` で RAM から予約 + `paging_verify_identity` | `pgalloc_arena_end()` = 予約後の `physmem_legacy_end` = **`workspace_first` と一致** (ホスト試験で等式を確かめる = 回帰なし) |
| それ以外 (8MB・9MB・12MB。**高位 RAM は無い** — 低位が 15MB 未満なら高位は存在しない) | **FIXED** — `[0x2F9000, 0x2FB000)` 8KB = metadata 1 ページ (0x2F9000) + workspace 1 ページ (0x2FA000)。定数 `MEM_LEDGER_META_BASE / END` | **新設の経路**: backing が `[MEM_LEDGER_META_BASE, MEM_LEDGER_META_END)` の内側であること、L0 でその範囲が RESERVED (RAM でない) であること、`paging_verify_identity` (present / RW / supervisor) — `physmem_reserve_ram` は呼ばない (RAM ではないので)。`init_model` の `first < MEM_EXEC_LOAD_ADDR` と `sys_memory_bootstrap_model` の「legacy 上端に接する」「workspace は exec 最小域より上」の検査は **ARENA_TOP のときだけ** | `pgalloc_arena_end()` = 低位 RAM の上端 (8MB なら 0x800000)。**backing の位置と無関係** |

- 8MB 型の metadata は常に 1 ページ: 上端 3,073 PFN で L1 776B + L2 3,073B = 3,849B ≤ 4,096B (`STATIC_ASSERT` に `pgalloc_metadata_bytes` の式で書く)。workspace は 1 ページ (`memory_boot_workspace_pages` = `MEMORY_BOOT_WORKSPACE_PAGES` 1、32MB 以下、`memory_boot.c:117-124`)。
- `sys_frozen_exec = pgalloc_arena_end() * PAGE_SIZE` (`sys.c:104` の `workspace_first * PAGE_SIZE` を置き換える)。ARENA_TOP では同じ値、FIXED では低位 RAM の上端。`sys_usable_mem_end` の利用者 (`exec_child_claim`、`ring3_band_ram_top`、`sys_reserve_top` (T1e で撤去)) は変えない。
- どちらの置き場も `sys_memory_bootstrap_model` → `sys_memory_stage_online` の同じ経路を通す (`pgalloc_init_layout` の制約を backing の種類で分ける)。**legacy の `pgalloc_init` と `legacy_metadata` は消す。** 8MB は高位 RAM が無いので stage は「全 eligible が恒等で張られていることの検証」だけになる (`pgalloc.c:209-211`)。
- 移行境界 (3,073 / 3,074 PFN) と 12MB 前後はホスト試験で押さえる (`test_memory_boot.py`、Codex P3)。
- T1-U1 (8MB 機でアプリの identity 写像が backing に届かない) は PDE 0 が supervisor なので成り立つ — kselftest の MM 検査 (`MM_RW` vs `MM_RWU`) に `[0x2F9000, 0x2FB000)` を足す。

### 3-4. P2V / V2P の定義と検査

**定義** (`include/memmap.h` に 1 か所、C11 の `static inline` — T0 で gnu11 になった。`uptr` は **`include/types.h` に `typedef unsigned long uptr;` を足す** (32 ビット限定の決定 2026-09-17 の範囲で、ポインタ幅の整数という意味の印)):

```c
/* v3: 恒等の supervisor 領域 (カーネル帯・シェル帯・池・低位・MMIO の恒等窓) だけ。
 * アプリ帯・lease 窓の番地は渡さない (as_va_to_pa / SURFACE から引く)。 */
static inline void *P2V(u32 pa)            { return (void *)(uptr)pa; }
static inline volatile void *P2V_IO(u32 pa){ return (volatile void *)(uptr)pa; }
static inline u32  V2P(const volatile void *va) { return (u32)(uptr)va; }
/* 静的初期化子・マクロ定義など「定数式」が要る場所だけ (B6)。関数の中では使わない。 */
#define P2V_CONST(pa)    ((void *)(uptr)(pa))
#define P2V_IO_CONST(pa) ((volatile void *)(uptr)(pa))
```

- **静的初期化子は `P2V_CONST`** (Codex B6): §2-2 の 10. の 10 か所 (`gfx_core.c:17-20`、`kcg.c:52-55`、`utf8.c:32`、`backend_pc98.c:154`) とマクロ定義 2 か所 (`isr_handlers.c:58-59`、`kcg.c:215`) はこれで書く (例 `u8 *bb_b = P2V_CONST(MEM_GFX_BB_BASE);`)。初期化処理へ移す案は採らない (`.data` の初期値が変わり、`objdump -s -j .data` の比較が効かなくなる)。`check_p2v.py` は **`P2V_CONST` / `P2V_IO_CONST` を関数本体の中で見つけたら違反**にする (定数式が要らない場所で使わせない — 判定は「行頭がインデントされていない = ファイルスコープ」か「`#define` 行」か「`static const struct` の初期化子ブロックの中」)。
- `boot_ptr` (`memory_boot.c:26`) の「GCC の NULL 付近参照の誤検出を隠す」空 asm は `P2V_IO` の側に寄せる (BDA 0594h・0000:0400h 系で同じ警告が出うる)。**`paging_init` の前でも使える** (恒等なので)。v4 で高位化するとき、ページング前の P2V は別名 (`BOOT_P2V`) に分ける — T1 では区別の印だけ付ける (呼び手を `P2V_BOOT` と名付けて数える)。
- **アプリ帯・lease 窓は別の関数**: `as_va_to_pa(as, va, *pa)` (PTE を辿る。今 `exec.c:816-829` にある PD / PT の物理歩きを `paging.c` へ寄せて作る。**USER・RW の判定は元のまま持ち越し、ホスト試験 `test_paging_bounds.py` に USER / RW の 4 通りを足す**) と SURFACE の `first` (§3-1)。`V2P` にはこれらの番地を渡せない (検査する、下)。
- **DMA には物理の記述子を渡す**: `dma_alloc` の戻りは `{pa, va}` の組 (§3-7)。FDC の BSS バッファ (`fdc.c:54`) は `V2P(s_fdbuf)` にし、`V2P` の結果に 16MB 未満・64KB 非またぎの検査をかける (今は `fdc_buf_layout` `fdc.c:63` が見ている)。
- **恒等を表明している箇所** (`paging_verify_identity`、`kselftest.c:884-885` の `pa == (u32)a`、`pgalloc.c:114`) は包まずに意味を書き直す: 「`P2V(pa)` と一致する」へ。

**検査** (`make check` に入れる):

- 規則 ID を [`../../CONSTRAINTS.md`](../../CONSTRAINTS.md) に新設 (案: **[C5]「物理番地をポインタにするのは `P2V` (定数式は `P2V_CONST`)、ポインタを装置・PTE・CR3 に渡すのは `V2P`」**)。`tools/check_constraints.py` は今 **ID の整合だけ**を見る (正典と `CLAUDE.md` の突き合わせ) ので、ID を足せばそこで整合が検査される。**通常の `make check` と `make check-changed` の両方から `check-p2v` に到達すること**を `build/sdk.mk` の依存で確かめる (X16)。
- **走査は新しい `tools/check_p2v.py`** (`make check-p2v`、`check` と `check-changed` の依存へ)。形は既存の `tools/check_le_access.py` と同じ 2 段:
  1. **文字列検査** — `kernel/ drivers/ gfx/ fs/ exec/ kapi/ lib/` で、ポインタ型へのキャスト `(T *)` / `(volatile T *)` / `(void *)` が (a) `include/memmap.h` の番地定数 (`MEM_*_BASE`、`MEM_*_ADDR`、`TVRAM_*`、`VRAM_PLANE_*`、`PEGC_*_BASE`、`BIOS_WORK_*`) か、(b) 名前が `*_phys` / `phys` / `pa` / `paddr` / `pfn` の式に当たっていたら、`P2V(` / `P2V_CONST(` の引数でない限り違反。`P2V_CONST` は上の場所以外で違反。逆向き: `(u32)` へのキャストが PTE / CR3 / DMA の口 (`paging_*` の物理引数、`dma_chan_setup`、`dma_setup`、`arch_mmu_load_root`) に直接渡っていたら違反。**アプリ帯・lease 窓の名前** (`RING3_*`、`MEM_EXEC_LOAD_ADDR`、`MEM_APP_BAND_*`、`MEM_LEASE_*`、`uva`、`user_*`) を `V2P(` に渡したら違反 (X7-3)。
  2. **例外一覧** — `tools/check_p2v_allow.txt` に `file:関数:理由` で列挙 (V86 のゲスト線形、リンカ由来の仮想 (`MEM_SHM_BASE`、`KHEAP_BASE`)、`kmalloc` の算術など §2-3 の「対象外」)。行番号でなく関数名で持つ (行がずれても壊れない)。
- 文字列検査は取りこぼし得る (2 行に分けたキャスト)。それは T1f の受入の**変異試験**で測る: 既知の違反 4 形 (定数キャスト・`*_phys` キャスト・`V2P(RING3_…)`・**関数内の `P2V_CONST`**) を写しの木に注入して検査が落ちることを見る (`tools/tests/mutpar.py` の流儀、実物のソースは書き換えない)。

### 3-5. 割り込み中の確保・解放を数える (R1)

- **観測用の深さを新設し、broker の判定は触らない** (Codex B1、X13): `kctx_irq_depth` (新設)。**全部の IRQ スタブ** (`isr_stub.asm` の共通スタブ `irq_stub_common_*` と IRQ0 / 1 / 2 / 4 / 7 / 11 / 12 / 13 の専用スタブ) の入口で +1、`popad; iretd` (`IRETD_USER`) の直前で −1 (アセンブラのマクロ `IRQ_ENTER` / `IRQ_LEAVE`)。**マクロは `inc dword [ss:kctx_irq_depth]` / `dec dword [ss:…]` と `ss:` セグメントで書く** — V86 / CPL=3 からの入口では TSS の `ss0` = 0x10 (`tss.c:36`)、CPL=0 からの入口では既存の SS (GDT ロード時の flat SS 0x10、`x86_desc.h:24`) なので、どちらでも `ss:` は同じ flat 番地を指し、`RESTORE_KSEG` の前でも安全 (§2-2 の 15.)。IRQ7 の専用スタブ (`:567`、`RESTORE_KSEG` 無し、`push eax` だけ) にもそのまま入る (§2-2 の 15.)。例外スタブの `EXC_ENTER` / `EXC_LEAVE` も同じ形。IRQ1 の `ring3_abort_check` (`isr_stub.asm:538`) と V86 の脱出 (`:483`、`:524`) は `IRQ_LEAVE` の**前**に置いたまま — つまり深さ > 0 で走る (R1 の「割り込みフレームの上で走る `ring3_abort_check`」を数えるため)。**`irq_in_irq` (`irq.c:25`) はそのまま残し、`irq_register` / `irq_unregister` の `IRQ_ERR_CTX` 判定は今までどおり共通 dispatcher の深さだけを見る** — 変えると PCM の CTRL+STOP 回収で `irq_unregister` が断られ、次の `pcm_open` が重複登録で失敗する (§2-2 の 3.)。broker の判定を全 IRQ の深さに揃えるのは、強制脱出を (a) 移譲 / (b) 回収に割った後 (T2、§1-2)。
- **例外の深さは全例外の入口で数える** (Codex B7、X12): `kctx_exc_depth`。入口は §2-2 の 4. の全部 — `isr_common` (全ベクタ)、`isr_stub_13` の V86 分岐、`isr_stub_14` の両分岐、`ISR_NOERR_V86` / `ISR_ERR_V86` の V86 分岐、`isr_stub_default` — で +1、**復帰する経路** (`IRETD_USER` / `iretd` の直前: 通常の例外ハンドラからの復帰、V86 の #GP 反射 `v86_gp_handler` が 0 を返した復帰) で −1、**longjmp する経路** (`ring3_fault_kill`、`v86_exit_to_kernel`) は下の規則で戻す。#PF / #GP だけを数えると #DE / #UD の fault kill が「通常文脈」に見える (反例: CPL=3 のゼロ除算)。
- **longjmp で抜ける経路は深さを setjmp の控えへ戻す — 控えは jmpbuf に入れる**: `KSETJMP_BUF_LEN` を 6 → **8** にし (`include/ksetjmp.h:18`)、`exec_setjmp` (`kernel/setjmp.asm:23`) が `kctx_irq_depth` / `kctx_exc_depth` を語 6・7 に保存し、`exec_longjmp` (`:43`) が復元する (`setjmp.asm:8-10` の配置コメントも直す)。**保存先は 3 か所で、全部を `KSETJMP_BUF_LEN` に統一する** (Codex B8): `AppSlot.jmpbuf[KSETJMP_BUF_LEN]` (`appslot.h:102`、そのまま)、`g_exit_jmpbuf[KSETJMP_BUF_LEN]` (`exec.c:1316`、そのまま。`:1360` の写しは `KSETJMP_BUF_LEN` 語なので新しい総語数で写る)、**`v86_jmpbuf[6]` (`v86.c:59`) → `[KSETJMP_BUF_LEN]`** — この 1 つが直書きで、直さないと `v86 -t` → `v86_run_limit` → `exec_setjmp(v86_jmpbuf)` (`v86.c:560`) が保存先を越えて書く。同じ段 (T1b) で直し、`grep -n 'jmpbuf\[' kernel exec include` に数字の直書きが残らないことを `check-changed` の grep 検査に足す。setjmp 点は 3 つ (`exec.c:1979` `ctx->jmpbuf`、`:2528` `a->jmpbuf`、`v86.c:560` `v86_jmpbuf`)、longjmp 点は 6 つ (`exec.c:1389` `g_exit_jmpbuf`、`:2306,2357,2416,2474` の park / kill 系、`v86.c:506` `v86_longjmp_out`)。この形なら**新しい longjmp 点が増えても漏れない** (T1-R3)。戻さないと以後の通常文脈が全部「割り込み中」に数えられる。
- **台帳の mutator** (`pgalloc_alloc_n_owner` / `_free_n_owner` / `ledger_transfer` / `ledger_reclaim_owner` / `ledger_claim_fixed` / `ledger_reserve_set`) の入口で `kctx_irq_depth > 0` なら `ledger_irq_ops++`、owner の `alloc_irq` / `free_irq` を +1、最後の 1 件の `{op, owner, 呼び出し元 EIP}` を控える。`kctx_exc_depth > 0` なら同様に `ledger_exc_ops`。両方 > 0 (IRQ の中で例外) はそれぞれに数える。**DMA プールの内部の割当は数えない** (R4: 固定プールの内側は利用時・IRQ からでよい)。
- **期待値** (今の経路のまま数えるので 0 ではない — T2 で 0 にする): CUI の無限ループ中の CTRL+STOP → `ledger_irq_ops` > 0 (IRQ1 スタブの上で AS 破棄)、`fault_test` の #PF / #GP / **#DE / #UD** → `ledger_exc_ops` > 0、PCM 再生中の CTRL+STOP → `ledger_irq_ops` > 0 で **`irq_ctx_violations` (`irq.c:23`) は増えない** (broker の判定を変えていない証拠)、V86 の終了 → 0 (teardown は longjmp の後、§2-1)、通常のアプリ終了 → 0。
- **報告**: kselftest の 1 行 (件数) と `mem` の表示、カーネルシンボルとして公開 (`kselftest_pass` と同じく `/api/mem` で読む)。**T1 の受入は件数の報告** (上の期待値と一致すること)。T2 で経路を割った後に panic へ切り替える。

### 3-6. `sys_reserve_top` の撤去と CPL=0 子のアリーナ (T1 の間の暫定)

`sys_reserve_top` は「使用可能上限を下げて、その下を永久予約する」2 つの役を兼ねる (`sys.c:185-214`)。T1 は後者を台帳の連続確保 (owner = boot) に置き換える。**前者 (上限を下げる) は CPL=0 の子のアリーナ (`exec_child_claim`、T2 で撤去) が要る**: CPL=0 の子は `[MEM_EXEC_LOAD_ADDR, sys_usable_mem_end())` を identity で丸ごと使い、スタックは上端から下へ伸びる。BB をこの中に置くと子が上書きする。

- **BB の探索範囲は legacy アリーナに限る** (Codex X14): `pgalloc_alloc_n_owner(boot, n, MEM_EXEC_LOAD_ADDR / PAGE_SIZE, pgalloc_arena_end(), TOP_DOWN, …)`。全池を TOP_DOWN で探すと 17MB / 64MB では BB が高位 RAM 側に行き、今の区間 (凍結した exec 上端の直下、`sys_reserve_top` の `ceiling - need`) と変わってしまう。アリーナ内の上端から取れば今と同じ区間になる (回帰なし — T1e の NP21/W 回帰で BB の物理番地が今と一致することを見る)。
- `ledger_arena_top()` = アリーナ `[MEM_EXEC_LOAD_ADDR, pgalloc_arena_end())` 内の **PERSIST owner の確保の最下端**。**永続確保が 1 つも無ければ `pgalloc_arena_end()`** (= 凍結した exec 上端そのもの)。⑥ で 1 回だけ決めて凍結する。`sys_usable_mem_end()` = `min(sys_frozen_exec, ledger_arena_top())`。
- **claim は owner 付き — 既存の入口判定はそのまま**: 入口の `appslot_cpl0_admit` (`appslot.c:178-184`: GUI からは `OS32_ERR_INVAL`、生存 CPL=3 アプリがあれば `OS32_ERR_FULL`) とそのエラーコード・表示 (`exec.c:1775-1780`)・試験 20A〜20E は**変えない**。その後ろの `pgalloc_mark_used` による claim を `ledger_claim_fixed(cpl0 子の暫定 owner, …)` にし、**他 owner のページが混じれば claim を拒否**して `exec` は **ロードを始める前に** `EXEC_ERR_NOMEM` で子を断る (`claim_refused` を数える)。入口判定が通った後に他 owner のページがアリーナに残っている状況は設計上無い (生存アプリ 0 本、GUI 外) ので、これは**第二の防御線**であって挙動の変化ではない (Codex 2 回目 P3 で 1 回目の記述を訂正)。受入は **`--cpl0` を付けた試験バイナリ** (`userland/tests/cpl0_probe`、`mkos32x --cpl0`、`deploy.yaml` に登録 [V2]) で行う — `v86` は `cui` 宣言の CPL=3 (`build/app.conf:85`) なので CPL=0 claim の受入にならず、今のツリーに `--cpl0` の本体バイナリは無い (§2-1)。
- T2 で CPL=0 子と claim を消すとき、`ledger_arena_top` も消す。

### 3-7. `dma_alloc` と 64KB 境界 (R4、D5、[HW2])

- 口: `int dma_alloc(u32 size, u32 align, u32 limit, struct dma_buf *out)` / `int dma_free(const struct dma_buf *b)` / `dma_mark_leaked`。`struct dma_buf { u32 pa; void *va; u32 size; }`。`limit` は物理の上限 (ISA の 8237 は 16MB = `DMA_PHYS_LIMIT`)、**64KB をまたがないことは常に条件** ([HW2])。失敗は負 (`OS32_ERR_*`)、**`*out` は不変 — 局所変数で受けて成功時だけ写す** (今の `dma_pool_alloc` は失敗時にも `*phys_out = 0` を書く (`dma_pool.c:37`) ので、その出力を `*out` に直接つながない、Codex P3)。
- 実体は **既存の `dma_pool_state_alloc` (`kernel/dma_pool_math.c`、ホスト試験つき) をそのまま使う** — 最初適合 + 整列 + 64KB またぎの検査は既にある。足すのは `limit` の検査と `{pa, va}` の組 (va = `P2V(pa)`) だけ。`dma_pool_alloc` の呼び手は PCM (`pcm_cs4231.c:525`) と kselftest だけなので置き換えて消す。
- **池を触らない** (固定の DMA 区間の内側だけ)。IRQ から呼べる性質 (`dma_pool.c:3-5`) は保つ。
- kselftest (R4): 「PCM リング 16KB (`PCM_RING_BYTES`) + 82557 CB/RFD ≒16KB の最悪の並び」— 32KB ずつの 2 半面に 16KB + 16KB を、先に 8KB の穴を開けた状態も含めて置けること、どの結果も 64KB をまたがず `limit` 未満であること。T3 で池が 0x3E0000 (64KB 整列) へ移っても同じ試験が通る形にする (池の番地を試験に焼かない)。

### 3-8. gfx: 副作用のない識別 → 予約 → 写像 → (probe) と BB

⑥ の中身 (§3-3)。**予約・写像は識別だけで決め、probe を呼ばない** (D33)。

**候補の予約は 1 owner・1 回の一括 commit** (Codex B3): PEGC のリニア窓 `[0xF00000, 0xF80000)` と Xe10 の銀行窓 `[0xF60000, 0xF68000)` は**重なる** (同じ 15〜16MB のシステム空間を両方が decode する — 実機でも PEGC のリニアを ON にしたまま WAB の銀行窓は使えない)。候補ごとに別の DEVICE owner で予約すると 2 本目が「他 owner との重複」で拒否され、Xe10 を先に予約して probe に失敗すると永久保持した銀行窓が PEGC への fallback を妨げる。→ **候補の窓は全部まとめて owner = DEVICE `gfx` (固定番号 7) の 1 回の `ledger_reserve_set`** に入れる。**資源レコードは 3 本、span も 3 本で、各 span が自分のレコードを指す** (Codex B10、3 回目で Xe10 を 2 レコードに分割): レコードは **PEGC** (bus = FIXED、DATASHEET、decode `[0xF00000, 0xF80000)`)、**Xe10-bank** (bus = CBUS、GLUE_CONST、decode `[0xF60000, 0xF68000)`、写像なし)、**Xe10-linear** (bus = CBUS、GLUE_CONST、decode `[0xFE000000, 0xFE400000)`、写像 `[0xFE000000, 0xFE200000)`)。span は `{PEGC 窓, res=PEGC}`、`{Xe10 銀行窓, res=Xe10-bank}`、`{Xe10 リニア decode, res=Xe10-linear}`。照合は span 単位で正規化の前 (PEGC のレコードでリニア窓を許すことも、Xe10-bank のレコードでリニア窓や PEGC 全体を許すこともしない)。その後の正規化で PEGC + Xe10 銀行窓は 1 区間 `[0xF00000, 0xF80000)` に併合され、**低位区間の `res_mask` は {PEGC, Xe10-bank}、高位区間 `[0xFE000000, 0xFE400000)` は {Xe10-linear}**。この形で冪等判定 (正規化後の区間集合と `res_mask` の一致) も成り立つ。probe でどちらかに落ちても予約は残る (永久保持。15〜16MB と 0xFE000000 の帯は RAM でないので害はない)。同じ組で再度呼べば冪等。**`gfx` は候補群の owner であって装置の存在の証明ではない** (DEVICE_RESERVATION §6)。

| 候補 | 識別 (副作用なし) | 予約 (`gfx` の一括 commit に入れる span) | 写像 (probe の前、写像範囲だけ) | BB |
|---|---|---|---|---|
| PEGC | BDA 0x045C bit6 / 0x0597 bit2 を読むだけ (`backend_pegc.c:134 bios_flag`、今も probe の最初に読んでいる)、かつ 15〜16MB に RAM 登録が無い (`pgalloc_range_has_ram`) | `PEGC_LINEAR_BASE` の 512KiB 全体 (表示 300KB だけにしない、DEVICE_RESERVATION §3)。資源レコード bus = FIXED、width_basis = DATASHEET | 窓全体 512KB を sup+PCD。今 `pegc_probe` の中にある map (`:586`) を外へ | 300KB (`MEM_GFX_BB8_SIZE`) を池から連続 TOP_DOWN (探索範囲は §3-6)、SURFACE (owner = boot) |
| Cirrus (Xe10) | **副作用のない識別手段が無い** (ID の読み出しが index OUT、`wab_glue_xe10.c`)。T1 は「`GFX=cirrus` / `auto` で、機種が 9821 系」までを識別とみなし、**予約は存在の証明ではない**と明記 (DEVICE_RESERVATION §6) | 銀行窓 `[0xF60000, 0xF68000)` (`WAB_XE10_WIN_BASE` / `_SIZE`) + リニア窓の decode `[0xFE000000, 0xFE400000)` (`WAB_XE10_LINEARWIN_DECODE` 4MB) — **span 2 本がそれぞれ Xe10-bank / Xe10-linear の別レコードを指す** (B10)。両レコードとも bus = CBUS、width_basis = GLUE_CONST (**値は NP21/W の窓の定数で、実機 Xe10 の実測ではない** — 実機の幅は P4 の実測 BAR と同じ扱い) | リニア窓の**写像範囲 2MB** `[0xFE000000, 0xFE200000)` (`WAB_XE10_LINEARWIN_SIZE`) を sup+PCD (今 `backend_cirrus.c:335` の probe 後 → 前へ。decode 4MB と写像 2MB の分離が X4 の実例)。probe が失敗しても予約と写像は永久保持 | 0 (RAM は使わない)。**面は MMIO backing の SURFACE として T1e で登録する** (B9): CLIENT `[0xFE04B000, +300KB)` (`CIRRUS_CLIENT_OFF`、75 ページ、owner boot → gshell、perm_max = RW、cache UC) と DISPLAY `[0xFE000000, +300KB)` (`CIRRUS_VIS_OFF`、owner kernel、perm_max = none)。位置は glue の定数から ⑥ で決まる (probe を待たない) |
| planar | 常に | — (VRAM は固定区間) | — | 固定 0x6A000 を SURFACE (FIXED_RAM) として登録 |

- **BB の量は候補の最大** (`GFX=` と識別で残った候補のうち): auto で PEGC 候補が残れば 300KB。Cirrus が probe に失敗して PEGC へ落ちても確保済みの 300KB の内側 (X7-1「足りない側へ落ちる組み合わせは無い」を、候補の最大を取ることで保証する)。
- **SURFACE の本数** (Codex P3、B9 で訂正): planar の固定 SURFACE (CLIENT、FIXED_RAM) は常に 1 本、PEGC の 8bpp BB (CLIENT、RAM backing) は候補に PEGC が残った起動で 1 本、**Cirrus は候補に残った起動で 2 本** (CLIENT + DISPLAY、MMIO backing)。auto の 9821 では最大 4 本が共存する (表は 8 本)。kselftest が見るのは「**選択中の backend の CLIENT** が SURFACE 表にあり、owner が boot (GUI 移譲後は gshell)」と「planar の固定 SURFACE が 1 本」。
- **`gfx_bb_phys_range` は「選択中 backend の CLIENT の SURFACE」を返す** (`{first × PAGE_SIZE, npages × PAGE_SIZE}`)。読み手は `exec.c:1959` (USER 昇格、`map_user_keep` で PCD を保つ) の 1 か所。backend 構造体の `bb_base` (仮想、CPU 描画の記述子 `fb->planes[0]` `gfx_core.c:124` が読む) は残し、値は `P2V(SURFACE.first × PAGE_SIZE)` で SURFACE から引く — 2 つの読み手が別の情報源を持たない。planar の常時 USER 写像 (`exec.c:1953-1956`) は今のまま。**Cirrus の probe が失敗して PC98 に落ちた起動では、Cirrus の SURFACE は表に残るが選択中でないので返らない** (今の `g_backend` の切り替えと同じ)。⑨ の移譲は選択中 backend の CLIENT だけ (owner boot → gshell)。
- BB の中身は確保の直後に 0 で埋める (`backend_pegc.c:699-704` の理由 — CPL=3 に USER で写るので前の中身を見せない — は T2 までそのまま生きる)。
- `pegc_reserve_backbuffer` は「台帳から受け取った SURFACE を使う」だけになり、`sys_reserve_top` と `pgalloc_mark_used` の呼び出しが消える。
- Cirrus の写像は ⑥ の 1 回だけ (T1-R5)。`backend_cirrus.c:320-331` の init 側の map と、`:327-329` の古いコメント (§2-2 の 7.) を消す。

---

## 4. 段の分割と受入

各段は単独で着地・回帰できる (前の段だけを前提にする)。完了条件はどの段も `make check-changed` rc=0 (コーダー)、着地で PM が `make all` + `make check` を 1 回。**順は T1a (モデル経路の一本化) → T1b (台帳の核) → T1c → T1d → T1e → T1f** (Codex B5: 台帳が metadata の形を変える前に legacy の backing を消しておく。逆順だと T1b 単独で 8MB が 1KB の `legacy_metadata` に L2 を書いて壊れる)。

### 4-0. 3 つのメモリ構成の確かめ方 (NP21/W の ini は [D2])

| 構成 | 手段 | 新しい ini |
|---|---|---|
| **8MB** | 既存の live 変更ツールの preset `ram-8mb` (`ExMemory 7`、スキル `os32-emu-config` §1)。今は legacy fallback を通す構成 — T1a の後はモデル経路になることを見る | 不要 (preset の適用は PM 所有で [D2] の承認対象) |
| **17MB** (低位 15MB + 高位 1MB、アドレス上端 17MB) | preset `ram-15mb` (`ExMemory 16`)。名前は「15MB」だが検出量は 17408KiB (`memory_boot.h:18-28`) | 不要 |
| **64MB** | **実機 Ra266** (64MB、[実機の物理操作は個別承認]) が 64MB の証拠。NP21/W の `ram-32mb` (`ExMemory 33`) と `ram-128mb` (`ExMemory 129`) は**補助** (32MB 以上で workspace が増える境界と、128MB の metadata の大きさを見る) で、**64MB の代替証拠ではない** (Codex P3) | 不要。NP21/W で 64MB ちょうどが要るなら preset `ram-64mb` の追加はツールの拡張 = 別タスク + [D2] (推奨しない — Ra266 が実物) |
| (参考) 9MB | 既存の試験用 ini `np21x64w-9mb.ini` (`ExMemory 8`、POLICY_DEBUG §4) | 不要 |
| (ホスト) 12MB 前後 | `test_memory_boot.py` で 3,073 / 3,074 PFN の境界 (§3-3) | — |

判定は**文言でなく番地**で読む: 新しい `kernel.map` の `pgalloc` の状態 (`model_mode` / `online`)、`sys_frozen_exec`、台帳の owner 表、`ledger_irq_ops` / `ledger_exc_ops` / `ledger_check_fail` を NP21/W では `/api/mem?space=phys` で ([V4]、CLAUDE.md の Known Gotchas「kselftest は新しい kernel.map の番地で読む」)、**実機では `/api/mem` が無いので `rshell_serial.py` 経由の `mem` (owner 表・容量・上端・ONLINE) と kselftest の出力行**で読む。記録するのは各構成の 容量・exec 上端・ONLINE・owner 会計。

### 4-1. T1a — モデル経路の一本化と物理側の定数 (旧 T1b)

| 項 | 内容 |
|---|---|
| 変更 | `kernel/memory_boot.c` (fallback の撤去、置き場の 2 択 §3-3)、`kernel/pgalloc.c` (`pgalloc_init` と `legacy_metadata` の撤去、`pgalloc_init_layout` の制約を backing の種類 ARENA_TOP / FIXED で分ける、`pgalloc_arena_end()` の追加)、`kernel/sys.c` (`sys_memory_bootstrap_model` の layout 検査を種類別に、**`sys_frozen_exec = pgalloc_arena_end() * PAGE_SIZE`**、B2)、`kernel/paging.c` (`[0x2F9000, 0x2FB000)` を present にする — FIXED 型のときだけ。0x2F8000 はガードのまま。**`reserve_table` の legacy 分岐 (`paging.c:401-420`、`pgalloc_alloc_n_pfn` で恒等ページを 1 枚ずつ) は到達不能になるので消す** — 全構成が workspace を持つ)、`userland/tests/cpl0_probe` (**`mkos32x --cpl0` の試験バイナリ**: 起動して自分のロード番地と `sys_usable_mem_end()` を表示して終わるだけ。`deploy.yaml` に登録 [V2]。今のツリーに `--cpl0` の本体バイナリが無いので受入のために足す)、`include/memmap.h` (**`MEM_LEDGER_META_BASE / END`**、**`MEM_POOL_BASE`** — `PGALLOC_BASE` 0x400000 を置き換える。**値は T1 では今の 0x400000 のまま、0x500000 にするのはシェル帯が 0x400000 へ来る T3** (§6 X9)、**`MEM_PHYS_RAM_CEILING` = 0x80000000** (D11。今は `MEM_DEVICE_APERTURE_BASE` = 0xFE000000、`memmap.h:389`)、`MEM_HIGH_RAM_BASE`、`MEM_BOOT_STAGING_BASE/SIZE` (0x500000 / 1MB)、`MEM_BOOT_BUNDLE_BASE/SIZE` (0x600000 / 1MB))、`tools/gen_memmap.py` (新定数と置き場の表示: 「カーネル予約 (上)」を 4KB ガード + 8KB 台帳 backing に割る)、`kernel/kselftest.c` (MM 検査に backing を足す) |
| D11 の影響 | 2GB 以上を RAM として登録しない (0594h の申告を 2GB で頭打ち、`memory_boot.c:82-83` の上限が変わる)。**`[2GB, 4GB)` の PCI BAR は MMIO として登録する** (U10) — 今は `[MEM_PHYS_RAM_CEILING, 4GiB)` を 1 回で MMIO にしている (`memory_boot.c:161`) ので、L0 では「RAM ではない」だけを持ち、区間の表 (T1b) で BACKGROUND と DEVICE を分ける |
| U24 | 0594h (1000000h 以降の容量、MB 単位) の意味を `docs/hw` の機種資料 (Bible + UNDOCUMENTED、矛盾時は UNDOCUMENTED) と照合し、結果を票に書く (資料そのものは著作権物なので写さない)。書き込み検証があるので結果が「違う」でも挙動は安全側 — 照合の結果で登録源を変えるかはそのとき判断 |
| ホスト試験 | `test_memory_boot.py` / `test_memmap_boot.py` / `test_highram_stage.py` / `test_pgalloc_model.py` を 8MB・9MB・12MB・**3,073 / 3,074 PFN**・15MB・17MB・32MB・33MB・64MB・128MB・2GB 超の申告で: **全部 model で ONLINE**、legacy の関数が存在しない、**FIXED 型の backing が `[MEM_LEDGER_META_BASE, MEM_LEDGER_META_END)` に収まり L0 で RESERVED であること** (RAM 側に置いた backing・範囲外の backing は拒否)、**`pgalloc_arena_end()`: ARENA_TOP では `workspace_first` と等しい、FIXED では低位 RAM の上端** (B2)、集積域 / 同梱域の規則 (bootinfo の申告あり / なし / 範囲外は拒否)、`physmem_add_trusted(MACHINE)` の呼び手が `memory_boot.c` だけ (grep の検査を試験に) |
| kselftest | `pgalloc_model_state() == PGALLOC_ONLINE` を起動のたびに見る (legacy に戻ったら fail)。**`sys_usable_mem_end() >= MEM_EXEC_LOAD_ADDR + MEM_EXEC_STACK_SIZE + MEM_EXEC_SBRK_MIN + MEM_EXEC_HEAP_MIN`** (exec の有効範囲が残る)。MM 検査に backing (RW、USER なし) |
| 変異 | 置き場の境界を 1 ページずらす → リンク ASSERT か地図検査 (`gen_memmap.py --check`) か試験が止める (§7 全構成の行)。`sys_frozen_exec` を `workspace_first` に戻す → 8MB のホスト試験が止める |
| NP21/W / 実機 | **8MB (`ram-8mb`) で planar と PEGC の両方** GUI 起動と GUI アプリ 1 本、**8MB で `sys_usable_mem_end() == 0x800000` を番地で読み、CPL=3 アプリ (`v86 -t` は CPL=3 なのでこちら) と CPL=0 の子 `cpl0_probe` (CUI から) が起動する** (B2。`v86 -t` は CPL=0 claim の受入にならない — Codex 2 回目 P3)、17MB (`ram-15mb`)、32MB / 128MB (`ram-32mb` / `ram-128mb`)、**Ra266 64MB** で起動 kselftest 0 fail と model ONLINE を番地で読む (D32 の受入) |
| 大きさ | カーネル本体の増分を測って票に書く (予算 13.0KB、§5 T1-R1)。`legacy_metadata` 1KB と `pgalloc_init` の撤去で減る側 |

#### 4-1-R. T1a の実装結果 (2026-09-30、`wt/t1a`、コーダー `claude-opus-5-5`)

**大きさ** (予算 13.0KB に対して): `__bss_end` 0x191C20 → **0x191820 (−1,024B)**、カーネル本体 583.0KB → **582.0KB (残り 14.0KB)**。内訳は `.text` +432B (FIXED 型の検証経路・`paging_map_ledger_backing`・`pgalloc_arena_end`・kselftest 2 件)、`.data` +40B、`.bss` −1,024B (`legacy_metadata`)。`pgalloc_init` の撤去で減った分は `.text` の中で相殺。

**U24 (0594h) の照合**: `docs/hw` の UNDOCUMENTED (`undocumented/memsys.md`) は 0000:0594h (WORD) を「1000000h (16MB) 以降の使用可能プロテクトモードメモリの容量、単位 MB」とし、有効な機種を PC-H98・PC-9821Af 以降の PC-9821・PC-9801BA2/BS2/BX2/BA3/BX3/BX4 としている — `memory_boot_detect` の読み方 (MB 単位、16MB 以降) と**一致**。Bible (`PC9800Bible/`) には 0594h の記載が無い (矛盾なし)。注意点が 2 つ: (1) 16MB 超対応の HIMEM.SYS は組み込み時にここを 0000h にする (一部の非対応版も 0 にする) — OS32 は自前のローダで起動し HIMEM を通らないので影響しない。(2) 上の機種以外では値の意味が保証されない — 書き込み検証 (1MB ごと + 別名の 2 巡目) があるので「無かった RAM」側にしか切り詰めない。**登録源は変えない** (0594h + 書き込み検証のまま)。あわせて 0401h の最大は 70h (14MB)、「16MB システム空間を使用しない」設定のときだけ 78h (15MB) — 15〜16MB を常にシステム空間として扱う今の設計と矛盾しない。

**ホスト試験** (`test_memory_boot.py`): FIXED = 8MB・9MB・12MB・3,073 PFN、ARENA_TOP = 3,074 PFN・15MB・16MB、高位 = 17MB・32MB・33MB・64MB・128MB、D11 = 3GB の申告 (登録は 2GB で頭打ち、`[2GB, 4GB)` は MMIO)。全部 model で ONLINE。FIXED は backing が `[0x2F9000, 0x2FB000)` で L0 RESERVED、`pgalloc_arena_end()` = 低位 RAM の上端 (8MB で `sys_usable_mem_end() == 0x800000`)、ARENA_TOP は `pgalloc_arena_end() == workspace_first`。FIXED の拒否 (RAM 側・範囲外 2 通り・ws 範囲外・metadata と ws の重なり・知らない種類・8MB の ARENA_TOP・置き場が模型で RAM (metadata 側 / ws 側)) は全件不変。grep 検査: legacy の関数・`legacy_metadata`・`PGALLOC_BASE` が無い、`reserve_table` が workspace だけ、`physmem_add_trusted` の呼び手は `memory_boot.c` の 1 か所 (`PHYSMEM_SOURCE_MACHINE`)。集積域・同梱域は定数の関係 (1MB ずつ・整列・隣接・8MB 内・shlib 帯より上) だけ (下の訂正 3)。
**変異** (`test_memory_boot.py --mutate`、`check-memory-host` で `$(MUT)`): §4-1 の 2 本 (置き場の境界を 1 ページずらす — BASE / END の 2 通り、STATIC_ASSERT でコンパイル不可 / `sys_frozen_exec` を `workspace_first` に戻す — 8MB で fail) + FIXED/ARENA_TOP 境界 (`>=` → `>` の片方向)・backing の張り忘れ・FIXED の RAM 拒否 2 本 = **7/7 RED**。`test_memmap_boot.py --mutate` に `ledger-blind` (backing の期待値を NP のまま) を足して 5/5 RED。 **Codex 実装レビュー (2026-09-30、Approve) の P3**: `memory_boot_detect()` のプローブ上限 (D11) を外す変異と、低位 RAM 0x590000 未満の fail-stop の境界を直接通す試験はまだ無い (静的には確認済み) — T1b の試験に足す。
**kselftest**: `pool:model online` (`pgalloc_model_state() == PGALLOC_ONLINE`)、`pool:exec range` (`sys_usable_mem_end()` ≥ exec の最小域)。backing の RW / USER なしは MM 検査 (`memmap_want_at` が FIXED のときだけ MM_RW を期待、T1-U1)。

**実装時の訂正** (設計と実物の食い違い):
1. **backing を張る時点**: §3-3 ② は `paging_init` と書いたが、FIXED / ARENA_TOP の判定は ③ (`memory_boot_init`) で決まるので、③ の中で bootstrap の直前に `paging_map_ledger_backing()` で張る (ブート文脈 = master CR3・live AS 0 の中、`paging_init` の後)。効果は同じ (FIXED のときだけ present、0x2F8000 は NP)。
2. **`MEM_HIGH_RAM_BASE` は既存** (K6)。`MEM_PHYS_RAM_CEILING` も既存で `MEM_DEVICE_APERTURE_BASE` (0xFE000000) の別名だったのを 0x80000000 に変えた (`test_physmem.py` の等式も直した)。
3. **集積域・同梱域の規則の試験 (bootinfo の申告あり / なし / 範囲外)** は T1a では書けない: bootinfo に同梱の申告欄が無く、規則そのもの (owner=bundle の予約) は §1-1 #7 で T1b の区間の登録。T1a は定数と静的な関係の試験だけ。規則と申告ありの試験は T1b へ。
4. **ホスト試験の足場**: 8 本のハーネスが旧 `pgalloc_init` を「メモリ量 kb の池を作る」足場に使っていたので `tools/tests/pgalloc_host_fixture.h` (試験の私有状態を直接組む、製品に口は足さない) に置き換えた。`paging_rebuild_host.c` の sparse / attrs / final (撤去した legacy の探索だけを突く、`make check` 未結線) は撤去し、nonmaster / rollback は workspace で組み直した。
5. **exec の最小域に満たない機械** (低位 RAM の上端 < 0x590000) は fail-stop になる (旧 legacy は小さな池で起動していた)。8MB 以上は影響なし。
6. **`cpl0_probe` は `sys_usable_mem_end()` を KAPI で読めない** (その KAPI は無く、足さない)。CPL=0 の子のスタック上端 = `sys_usable_mem_end()` (`exec.c` の `stack_top = mem_end`) なので、ESP をページへ切り上げた値を `usable_end` として表示する。

**T1a の NP21/W 回帰で見つかった漏れと修正** (2026-09-30、`wt/t1a-v86`、暫定修正 `0ad8d78` はコーダー `claude-opus-5-5`、Codex が Request changes (P1) → 最終修正はコーダー `claude-fable-5-1`): 8MB + PEGC で `v86 -t` が `FAILED (rc=-1)` (`v86_mem_setup` の `pgalloc_alloc_n(159)` が取れない)。**T1a の後退ではない** — PM の計測で、T1a 前 (`bffc639`、legacy 経路) も CPL=3 アプリを 1 本起動・終了するたびに `used_pages` が 74 ずつ増える (330 → 404 → 478 → 552 で `NOMEM`)。**原因**: CPL=3 アプリの私有領域の上端 (`exec/exec.c` の `ring3_band_set` が決める `g_ring3_band_top` = `RING3_USTACK_TOP`) が帯の上端 `MEM_APP_BAND_BASE + pdes × 4MB` = 0x800000 そのもので、RAM の上限を見ていなかった。8MB では PEGC の BB = `sys_reserve_top` の `[0x7B5000, 0x800000)` が**帯 1 枚の中**にあり、先に張った私有ページ (スタック `[0x7C0000, 0x800000)` の 64 + exec_heap の上端 `[0x7B5000, 0x7BF000)` の 10 = **74**) の PTE を、後から `gfx_bb_phys_range()` の範囲を `paging_addrspace_map_user_keep()` で恒等に写すときに BB の物理で上書きしていた。teardown は PTE を辿って BB の物理を返そうとし、pgalloc は予約済みとして黙って断るので私有の 74 ページは戻らない (同時にスタックが共有の BB と同じ物理になっていた)。17MB では BB (0xEB3000) がアプリ帯より上なので起きない。**暫定修正 (`0ad8d78`、撤回)**: BB のうち帯と重なる部分を写さない (`exec_map_shared_bb` の区間演算 + `exec_bb_clipped_pages`)。漏れは閉じたが Codex の P1: BB の参照側 (`gfx_get_framebuffer()` の 0x7B5000、全画面アプリの `gfx_init` → `pegc_init` がアプリ CR3 のまま行う BB クリア、`libos32gfx_attach()` と shlib の Rust `Painter`) が旧番地のままなので、8MB + PEGC で私有 exec_heap を BB として読み書きし、ガード 0x7BF000 で fault kill になる。**最終修正**: **私有領域の上端を BB の下に収め、BB は今の番地のまま共有で写す** (カーネル・アプリの BB ポインタは変えない)。`ring3_band_set(pdes)` が `g_ring3_band_top = min(帯の上端, sys_usable_mem_end())` にする — 8MB + PEGC では 0x7B5000 (スタック `[0x775000, 0x7B5000)`、ガード 0x774000、exec_heap はその下)、PDE の所有範囲 (`band_pdes` = `as.app_pde_count`) は帯 1 枚のまま。`AppSlot.band_top` は私有領域の上端 (= `stack_top`) の意味になり、teardown が返す 3 領域 (本体+sbrk / exec_heap / `[band_top − 256KB, band_top)`) は BB を含まない = **BB の物理を pgalloc へ返そうとしない**。BB の写像は `exec_map_shared_bb(as, user_top)` に戻し (恒等・丸ごと・`_keep` で PCD 保持)、BB が私有領域 `[MEM_APP_BAND_BASE, user_top)` と重なるときは写さずに起動を断る (`Error: backbuffer overlaps app area`、EXEC_ERR_NOMEM。通常は起きない fail-stop)。`exec_bb_clipped_pages` は撤去。17MB (BB 0xEB3000、帯の上)・12MB (0xBB5000)・9801 planar (0x6A000、帯の下)・Cirrus (デバイス窓) では `sys_usable_mem_end() ≥ 帯の上端` なので私有領域も BB の写り方も従来と同じ。2 枚 (heap_size 指定) のときは `paging_app_band_pdes` の `by_ram` が先に枚数を落とすので、上端の切り詰めが効くのは 1 枚のときだけ。`ring3_ptr_ok` の上端も一緒に下がる (8MB + PEGC で BB を指すポインタは KAPI の早期検証で弾かれる = 17MB と同じ振る舞い)。**D19 (BB は gshell 所有 + 全画面 lease) とは独立** — BB の置き場と番地を変えていないので、D19 の作り直しはこの上に載る。**試験**: `tools/tests/test_app_bb_overlap.py` (`check-memory-host`、実物の paging / pgalloc / physmem + `exec.c` から切り出した `ring3_band_set` / `app_map_region` / `exec_bb_overlaps_user` / `exec_map_shared_bb` / `exec_teardown_app`) — 8MB + BB 予約の池で (a) 起動・終了 ×10 の毎回 `used_pages` が戻り V86 の 159 の連続が取れる、(b) 私有 PTE が BB を指さない・恒等でない、(c) BB の仮想番地の PTE が BB の物理を恒等 + USER で指す、(d) 私有領域の上端 = 0x7B5000 < BB・PDE は 1 枚、(e) teardown が BB の物理を `pgalloc_free_page` に渡さない (paging.c の呼び出しを数える口)、(f) 上端が下がっていない形は起動を断り私有 PTE を触らない、(g) 17MB / 12MB / 9801 / Cirrus (PCD 付き) で上端が 0x800000 のまま・共有 PT に USER で写り PCD を保つ。`exec_launch()` の呼び出し箇所は切り出せないのでテキストで見る (`gfx_bb_phys_range()` の呼び出しは 1 個、`exec_map_shared_bb(&ctx->as, ctx->band_top)` が 1 個、素の `_keep` 呼び出しが無い)。変異 7/7 RED (5 本は実行で検出、呼び出し箇所の 2 本はテキスト検査で検出 — Codex 2 回目 P3 の訂正。上端の切り詰めを外す / 重なりの検査を外す / teardown が帯の上端を見る / BB を写さない / `_keep` でなく flags 上書き / 呼び出し箇所を旧実装へ戻す / 呼び出し箇所が帯の上端を渡す — コンパイルエラーは RED に数えない)。kselftest には足していない (ブート時は CPL=3 アプリも BB の写像も無く、起動・終了の往復は host 試験で見る)。**NP21/W の 8MB で PM が確かめること**: `used_pages` の往復、`v86 -t`、CUI からの `pegcchk 3` (全画面 `gfx_init` が fault kill にならない)、GUI アプリ 1 本。 **結果 (PM、2026-10-01、NP21/W `ExMemory=7` + PEGC、`1a2d867` のカーネル)**: 起動直後 `used_pages` = 256 (修正前 330)、`test2` → `v86 -t` (OK) → `pegcchk 3` (`pattern drawn`、`fault_kill_count` = 0) の後も 256。GUI では 8MB で初めて gui_demo (Widgets / Help) が開いた。2 本目 (v12_api_test) は `Launch failed (... out of memory)` — 8MB の容量の上限で、v3 の最低条件 (TASK_MEMMAP_V3 §7) の対象。Codex (gpt-6-astra) は 1 回目 Request changes (P1) → 最終修正で Approve。

### 4-2. T1b — 台帳の核: owner・区間の表・R1 の計数・R5 / R7 (旧 T1a)

| 項 | 内容 |
|---|---|
| 前提 | T1a (全構成が model 経路。metadata の形を変えるのはここから — B5) |
| 変更 | `kernel/pgalloc.[ch]` (L2 の owner 配列を metadata へ、owner 表・区間の表・資源の表・SURFACE の表を BSS へ、owner 付き API、旧 API の撤去、`ledger_selfcheck`)、`kernel/physmem.[ch]` (変更なし予定)、`kernel/paging.c` (AS の PD / PT・`reserve_table`)、`exec/exec.c` (`app_map_region`・teardown・AS owner の取得 / 返却、CPL=0 claim を `ledger_claim_fixed` に §3-6 — 入口判定 `appslot_cpl0_admit` は不変)、`kernel/shlib.c`、`kernel/v86_mem.c` (`v86_mem_setup(owner, …)`)、`kernel/sys.c` (**暫定 `sys_reserve_top(owner, bytes)` — 永久予約の L2 を owner に**、B11)、`gfx/backend_pegc.c` (**`sys_reserve_top(boot, …)` を呼び、`pgalloc_mark_used` の行 `:698` は消す** — 今も無操作。置き場は T1e まで今のまま)、`kernel/isr_stub.asm` (`IRQ_ENTER` / `IRQ_LEAVE` / `EXC_ENTER` / `EXC_LEAVE`、`ss:` で書く、IRQ7 を含む全スタブ)、`kernel/setjmp.asm` + `include/ksetjmp.h` (深さの控え、`KSETJMP_BUF_LEN` 6 → 8) + **`kernel/v86.c:59` (`v86_jmpbuf[6]` → `[KSETJMP_BUF_LEN]`、B8)**、`kernel/irq.[ch]` (**`irq_in_irq` は残す**、B1)、`include/memmap.h` + `include/types.h` (P2V / V2P / `P2V_CONST` / `uptr` の定義 — 使うのはこの段で書き換えた呼び手だけ)、`kernel/kselftest.c`、`mem` コマンド (owner ごとの使用量) |
| ホスト試験 | 新規 `tools/tests/test_ledger.py` (`test_pgalloc_model.py` の実ソース ILP32 ハーネスを流用): owner 付き確保 / 解放 / 移譲 / 一括回収、他 owner 混在の解放は全件不変で拒否、`ledger_claim_fixed` の他 owner 混在の拒否、DEVICE / PERSIST owner の回収拒否、owner 表満杯、owner 番号の返却 (参照が残れば拒否) と再利用、allocated ⇔ owner ≠ 0 (途中失敗で bitmap と L2 が両方戻る、X11)、`pages` の合計、**合成モジュール owner の init 途中失敗 → 一括回収で 0** (R7)、**bundle → モジュール移譲の後の失敗は当該 owner だけ回収、bundle の残りは不変** (R7 往復 7 P2)、**会計は失敗時不変・診断カウンタは増える** (P3)。既存 `test_physmem` / `test_pgalloc_range` / `test_pgalloc_model` / `test_highram_stage` / `test_paging_bounds` は通ったまま |
| kselftest | `ledger_selfcheck("boot")`、AS を作って壊すと AS owner のページ 0 (R5 (a))、永続 owner の総量が起動直後から変わらない (R5 (b)・R6 の入口)、`kctx_irq_depth == 0 && kctx_exc_depth == 0` (通常文脈) |
| 変異 | owner 検査を外す (他 owner のページを解放できてしまう) / `IRQ_LEAVE` を 1 本抜く / 例外の復帰点の −1 を 1 つ抜く / 回収で DEVICE を拒否しない / `exec_longjmp` の深さ復元を抜く — をそれぞれ試験が止める |
| NP21/W 回帰 | 17MB (今の既定構成) と **8MB** (T1a の構成をそのまま) で起動 kselftest 0 fail、**PEGC で GUI 起動 (BB が boot owner で永久予約され claim が通る、B11)**、gshell 起動、GUI アプリ 3 本の起動・終了 ×10 で AS owner 0、**`cpl0_probe` を CUI から起動 → claim が通り `pages` が返る、GUI からは今までどおり `OS32_ERR_INVAL`**、CUI の無限ループ中の CTRL+STOP・`fault_test` の fault kill (**#PF・#GP に加えて #DE か #UD**、B7)・PCM 再生中の CTRL+STOP の**それぞれで `ledger_irq_ops` / `ledger_exc_ops` の件数を記録** (§3-5 の期待値と一致すること — 報告が受入、R1)、**PCM 再生中の CTRL+STOP の後に PCM を再オープンして再生でき、`irq_ctx_violations` が増えていない** (B1)、V86 (`v86 -t`) の起動と終了 (`ledger_irq_ops` が増えない — teardown は longjmp の後) |
| 大きさ | カーネル本体の増分を測って票に書く (予算 13.0KB、§5 T1-R1)。固定表 ≈3.0KB は BSS (§3-1)。owner / 区間 / 資源 / SURFACE の表の実サイズ (U1) |

#### 4-2-R. T1b の実装結果 (2026-09-30、`wt/t1b`、コーダー `claude-opus-5-5`)

**大きさ** (T1a 後の残り 14.0KB に対して): `__bss_end` 0x191820 → **0x193504 (+7,396B ≈ +7.2KB)**、カーネル本体 582.0KB → **589.3KB (残り 6.7KB)**。内訳は `.text` +5,456B (台帳の API・`ledger_selfcheck`・区間の登録・exec / paging / shlib / v86 の呼び手・kselftest)、`.data` +516B、`.bss` +3,300B。**固定表の実サイズ (U1)**: owner 24B × 64 = 1,536B、区間 16B × 48 = 768B、資源 32B × 16 = 512B、SURFACE 24B × 8 = 192B、計 **3,008B (BSS)** + 診断カウンタ約 60B (`STATIC_ASSERT` で 24 / 16 / 32 / 24B を止める)。**metadata (L1 + L2 = 1.25B/PFN)**: 8MB 2,560B (1 ページ)、12MB 3,840B、上端 3,073 PFN 3,849B (FIXED 型の 1 ページに入る — `STATIC_ASSERT(PGALLOC_META_BYTES(3073) <= PAGE_SIZE)`)、15MB 4,800B・17MB 5,440B (2 ページ)、64MB 20,480B (5 ページ)、2GB 655,360B (160 ページ)。表で覆える量 (`memory_boot_high_fit`) は約 2.8GB → 約 2.3GB に減るが、登録上限 2GB (D11) の方が先に効く (`test_memory_boot` の tables で確認)。

**実装したもの**: `kernel/pgalloc.[ch]` に L2 (owner 1B/PFN、metadata の 2 bitmap の直後) と固定表 4 本 (BSS)、owner の取得 / 返却 (`ledger_owner_new` / `_retire`、参照が残れば拒否して `ledger_retire_refused`、番号は再利用)、`pgalloc_alloc_n_owner` (TOP_DOWN / BOTTOM_UP) と `pgalloc_alloc_phys` (汎用の池、物理番地)、`pgalloc_free_n_owner` (他 owner 混在は全件不変で拒否、`ledger_bad_free`)、`ledger_transfer`、`ledger_reclaim_owner` (DEVICE / PERSIST・SURFACE を持つ AS は拒否)、`ledger_claim_fixed` (他 owner 混在は拒否、`ledger_claim_refused`)、`ledger_register_region` (起動時だけ、重なり拒否、OUTSIDE は自動)、`pgalloc_reserve_pfn(owner, …)` (PERSIST だけ、L2 に owner)、`ledger_selfcheck(tag)`。**旧 API (`pgalloc_alloc_page` / `_free_page` / `_alloc_n` / `_free_n` / `_alloc_n_range` / `_alloc_n_pfn` / `_free_n_pfn` / `_mark_used`) は撤去** (呼び手はリンクで追えた分を全部書き換え)。R1: `kctx_irq_depth` / `kctx_exc_depth` を全 IRQ スタブ (共通 + IRQ0/1/2/4/7/11/12/13) と全例外入口 (`isr_common`・#GP / #PF・`ISR_*_V86` の V86 分岐・`isr_stub_default`) で `ss:` の inc / dec、`KSETJMP_BUF_LEN` 6 → 8 で深さを jmpbuf の語 6・7 に控え `exec_longjmp` が戻す、`v86_jmpbuf[KSETJMP_BUF_LEN]` (B8)、`irq_in_irq` と broker の判定は不変 (B1)。起動: `memory_boot_init` の最後 (`stage_online` の後) に区間の表 (固定用途 12 本 + ARENA_TOP 型の backing + 背景 2 本) と集積域・同梱域の規則 (`memory_boot_boot_areas`、起動では申告なし)。§4-8 の T1b 列のとおり: shlib 帯は `ledger_claim_fixed(shlib)` + 失敗経路 11 か所 `pgalloc_free_n_owner(shlib, …)`、AS の PD / PT とアプリの私有ページと shlib data 複製は AS owner (exec が AS 作成の直前に取り `struct addrspace` の `owner` に持つ、teardown の最後に `ledger_reclaim_owner` で取り残しを掃除して `exec_as_leftover_pages` に数え、返却)、V86 バッキングは `v86_mem_setup(exec_ledger_owner())`、PEGC BB は `sys_reserve_top(boot, …)` (直後の `pgalloc_mark_used` は撤去)、CPL=0 claim は `ledger_claim_fixed` (入口判定 `appslot_cpl0_admit` は不変、他 owner 混在ならロード前に `EXEC_ERR_NOMEM`)、paging の自己診断 3 本は試験用 AS owner "pgtest" (B12)。`P2V` / `V2P` / `P2V_CONST` / `uptr` を定義し、使ったのは書き換えた行 (paging.c の PD / PT、shlib の複製) だけ。

**ホスト試験**: 新規 `tools/tests/test_ledger.py` (`check-memory-host`、`$(MUT)`) — 実ソース ILP32 で irq_restore の度に L1 / L2 / owner 会計の不変条件を全ページ検査する。確保 / 解放 / 移譲 / 一括回収、TOP_DOWN、他 owner 混在の解放・移譲・claim の全件不変 (会計の写しと突き合わせ、診断は増える)、DEVICE / PERSIST の回収拒否、owner 表満杯・返却 (pages / 区間 / SURFACE の参照で拒否)・再利用、R7 (合成モジュールの init 途中失敗 → 回収で 0、bundle → モジュール移譲後の失敗は当該 owner だけ、bundle の残りは不変)、R1 の計数 (IRQ / 例外 / 両方 / 通常)、区間の表と `ledger_selfcheck` の検出、asm の深さの置き場 (静的)、setjmp / longjmp の深さの控えと復元 (nasm で組んで実行)、jmpbuf の長さの直書きが無いこと (T1-R8)、broker の判定が `irq_in_irq` のまま (B1)、旧 API が無いこと。`test_memory_boot.py` に **T1a から回した 2 点** (D11 のプローブ上限 — BIOS ワークと書き込み検証をホストの配列で受けて 4000MB の申告が 2GB で止まる、低位 RAM 0x590000 の fail-stop の境界を両側で) と集積域・同梱域の規則 (申告なし / 範囲外 4 通り / 申告あり / 二度目 / 集積域は池のまま) と区間の表の検査を足した。既存の `test_pgalloc_model` / `test_pgalloc_range` / `test_highram_stage` / `test_paging_bounds` / `test_app_band_pde` / `test_device_reservation` / `test_memmap_boot` / `test_pegc_mode` は owner API に書き換えて通る (`test_pgalloc_model` の検証器にも L2 の不変条件を足した)。
**変異**: `test_ledger.py --mutate` 6/6 RED — owner 検査を外す (`page_owned`)、claim の owner 検査を外す、`IRQ_LEAVE` を 1 本抜く (IRQ4)、例外の復帰点の −1 を抜く (`isr_common`)、回収で DEVICE を拒否しない、`exec_longjmp` の深さ復元を抜く。**Codex 実装レビュー (Approve) の P3 で訂正**: 初版の「owner 検査を外す」は `page_owned` の引数が未使用になり `-Werror` のコンパイルエラーで RED になっていた — 変異側に `(void)owner;` を足し、組み立ての失敗は RED と数えない判定にした (6 本とも実行時の検査で RED: owner 検査 → 確保 / 解放 / 移譲 / 回収と R7 の本体、claim → 拒否の本体、asm 2 本 → 静的検査、longjmp → nasm で組んだ実行)。`test_memory_boot.py --mutate` 10/10 RED (T1a の 7 本 + プローブ上限を外す・最小域の境界を 1 ページずらす・同梱域の範囲検査を外す)。
**kselftest**: `ledger:ctx depth 0`、`ledger:selfcheck boot`、AS を作ってその owner で 2 ページ取り壊して回収すると AS owner 0 (R5 (a))・番号を返せる、永続 owner の総量がその間に変わらない (R5 (b))、1 行の件数表示 `[ledger] irq_ops= exc_ops= check_fail= bad_free=`。

**実装時の訂正** (設計と実物の食い違い):
1. **`mem` コマンドの owner ごとの表示は入れていない**: `mem` はシェルの組み込みで KAPI 経由でしか情報を取れず、owner 表を見せるには KAPI の追加が要る — §3-2 の「KAPI は変えない」と衝突する。T1b では kselftest の 1 行とカーネルシンボル (`ledger_owners` ほか、`/api/mem` / `kernel.map`) までにし、`ledger_dump(cb)` も呼び手が無いので足していない。KAPI を足すかは PM の判断。
2. **bootinfo と固定 PT の区間は外側の区間に含めた**: bootinfo (0x7E00) はフォントキャッシュ `[0x1000, 0x4A000)` の内側、固定 PT (`pd_raw` / `pt_raw`) はカーネル帯の BSS の内側で、別の区間にすると区間の非重複が崩れる。区間は NULL ガード・フォント・Unicode・planar BB (SURFACE_BACKING、owner boot)・低位の残り (mailbox 0x90000 を含む)・VRAM + ROM (UC)・カーネル帯・SQLite 帯・DMA・DMA 上側ガード + 台帳 backing・カーネルスタック (ガード込み)・シェル帯の 12 本。
3. **`kctx_irq_depth` / `kctx_exc_depth` の定義は `kernel/pgalloc.c`** (台帳の観測値。ホスト試験が pgalloc.c を取り込むだけで持てる)。`kernel/irq.[ch]` は変更なし。
4. **同梱域は `ledger_claim_fixed` ではなく連続確保 (`pgalloc_alloc_n_owner(bundle, 256, …)`) で押さえる**: claim は同 owner に冪等で eligible でないページを飛ばすので、区間の登録に失敗したときの巻き戻しが正確でない。全ページが空きの RAM のときだけ通り、二度目の申告は何も変えずに断る。
5. **永久予約 `pgalloc_reserve_pfn(owner, …)` は PERSIST だけ**、既に L2 に owner の付いたページ (他の永久予約) に重なれば拒否 (旧版は既に eligible でないページを黙って許した)。AS / MODULE の L2 に永久予約が無いので、回収は eligible かつ allocated のページだけを見ればよい。
6. **汎用の池の確保口 `pgalloc_alloc_phys(owner, n)` を足した** (§3-2 の表に無い): 旧 `pgalloc_alloc_n` / `_page` の呼び手 (paging・exec・shlib・v86_mem) は物理番地で持つ。探索範囲は旧と同じ `[MEM_POOL_BASE, 上端)` の下からの最初適合。
7. **metadata が L2 の分大きくなり、ARENA_TOP 型 (低位 15MB) では exec の上端が 1 ページ下がる**: 16MB / 17MB で `sys_frozen_exec` 0xEFE000 → **0xEFD000** (metadata `[0xEFE, 0xF00)`、workspace 0xEFD)。PEGC の BB も 4KB 下がる (17MB で 0xEB3000 → 0xEB2000)。FIXED 型 (8MB〜12MB) は不変 (8MB は 0x800000 のまま)、3,073 / 3,074 PFN の境界も不変。
8. **CPL=0 の claim の owner は AS 種別の動的 owner "cpl0"** (最初の子で取り、最後の子で `ledger_reclaim_owner` → 返却)。release は A / B の `pgalloc_free_n_owner` でなく一括回収 (claim で取ったページは全部その owner のもの)。
9. **AS の取り残しの件数は `exec_as_leftover_pages`** (teardown の最後の回収で返ったページの累計、0 が正常)。`ledger_reclaim_pages` は CPL=0 の release も含む全回収の累計。
10. **資源の表と SURFACE の表は型と BSS の置き場だけ** (登録 API は T1d / T1e)。返却・回収・自己検査は SURFACE の表を見る。

#### 4-2-N. T1a・T1b の NP21/W 回帰 (PM、2026-10-01、main `d7ac7a0` = T1a + BB 漏れの修正 + T1b)

新しい `kernel.elf` の nm で引いた番地を `/api/mem?space=phys` で読んだ。8MB は `ExMemory=7` (切り替えは `np21w_ini_live.py ram-8mb`、原本は `np21x64w.ini.np21w-live-b0081b…/original.bin`。道具は ini の書き換えまで済んだが再起動直前の `snapshot` 段で失敗 — 道具の不具合として別に直す)、17MB は元の `ExMemory=16` (原本を戻して一致を確認)。PEGC、`GFX=auto`。

| 構成 | 項目 | 結果 |
|---|---|---|
| 8MB | 起動 | `kselftest_fail`=0 (pass 252)、`ledger_check_fail`=0、`ledger_bad_free`=0、`sys_frozen_exec`=0x7B5000、`used_pages`=256 (`total_pages`=949)、`kctx_exc_depth`=0 |
| 8MB | CPL=3 の往復 (`test2` ×2・`v86 -t`) | `used_pages`=256 のまま、`exec_as_leftover_pages`=0、`v86 -t` OK |
| 8MB | `cpl0_probe` (CUI) | `cpl=0 usable_end=7b5000`、終了後 `g_cpl0_owner`=0、`ledger_claim_refused`=0 |
| 8MB | #PF kill (`ring3_fault`) | `fault_kill_count` +1、`ledger_exc_ops`=414 (>0)、`kctx_exc_depth`=0 に戻る、`used_pages`=256・leftover 0 |
| 8MB | `pegcchk 3` (CUI、全画面 gfx_init) | `pattern drawn`、`fault_kill_count` 増えず (BB 漏れの修正の確認) |
| 8MB | GUI | gshell と gui_demo が開く (8MB で初めて)。2 本目は `out of memory` — 容量の上限 (v3 の最低条件の対象) |
| 17MB | 起動 | `kselftest_fail`=0、`sys_frozen_exec`=0xEB2000 (§4-2-R の予告どおり 1 ページ下がる)、`used_pages`=256 (`total_pages`=2994)。`irq_ctx_violations`=1 は起動直後から (CTRL+STOP 由来ではない) |
| 17MB | ゲスト試験一式 (`tools/guest_tests.py`) | 16 件中 PASS 14、SKIP 2 (`e2test` の 372KB、`host_test` の host_agent — 以前からの前提不足) |
| 17MB | GUI (`gui_gate.py v12g4 --h 480`) | RESULT: OK、窓 4 本・アプリからの起動・CUI への戻りをスクリーンショットで確認 |
| 17MB | 試験一式 + GUI の後 | `used_pages`=256、`exec_as_leftover_pages`=0 |

**未確認** (次の段で取り直す): CUI のアプリ実行中の CTRL+STOP (`sleep 20` 中に `/api/key CTRL+STOP` を送ったが、送る時点が早すぎた可能性があり `ledger_irq_ops`=0 のまま — 専用の無限ループ試験バイナリと手順で)、#GP と #DE / #UD の kill (起こす試験バイナリが無い)、PCM 再生中の CTRL+STOP → 再オープン・再生 (NP21/W のこの構成は `[pcm] none` — 実機か PCM のある構成で)、実機 Ra266 64MB。

**未確認の取り直しの手順** (試験バイナリ `faulttest`、`userland/tests/faulttest.c`、`/usr/bin/` [test])。番地は毎回**新しい** `kernel.elf` の nm で引く (`used_pages` は `pgalloc.c` の static、`exec_as_leftover_pages` は `exec/exec.c`)。各段の前後で `fault_kill_count`・`ledger_exc_ops`・`kctx_exc_depth`・`ledger_irq_ops`・`kctx_irq_depth`・`used_pages`・`exec_as_leftover_pages` を読む。

| 段 | コマンド (CUI) | 起こすもの | 合格 |
|---|---|---|---|
| #GP | `faulttest gp` | CPL=3 の `hlt` (CPL≠0 なら IOPL に関係なく #GP(0)。`cli` は OS32 が IOPL=0 で降ろすから #GP になるだけなので使わない) | `faulttest: raising #GP` の後にシリアルへ `[ring3] exception ... vec=0000000D`、`SURVIVED` が出ない、`fault_kill_count` +1、`ledger_exc_ops` 増、`kctx_exc_depth`=0 に戻る、`used_pages` が試験前と同じ・leftover 0 |
| #DE | `faulttest de` | asm の `divl` で 0 除算 | 同上 (vec=00000000) |
| #UD | `faulttest ud` | `ud2` | 同上 (vec=00000006) |
| #PF | `faulttest pf` | カーネル帯 (`KERNEL_LOAD_ADDR`) へ書く — `ring3_fault` と同じ、比較用 | 同上 (`[ring3] #PF`) |
| CTRL+STOP (純ループ) | `faulttest loop` → 画面に `faulttest: looping (CTRL+STOP to kill)` が出たのを `/api/tvram` か `/api/screenshot` で確かめてから `/api/key` で CTRL+STOP | KAPI を呼ばない CPL=3 の `jmp` ループ。止めるのは IRQ 出口の `ring3_abort_check` (`isr_stub.asm`、割り込まれた CS.RPL=3 のときだけ) | シェルに戻る、`ledger_irq_ops` 増 (IRQ の上での回収)、`kctx_irq_depth`=0 に戻る、`used_pages` 同じ・leftover 0。`fault_kill_count` は CTRL+STOP では増えない想定 (増えたら記録) |
| CTRL+STOP (KAPI 連打) | `faulttest kloop` → `faulttest: kloop get_tick (CTRL+STOP to kill)` を確かめてから CTRL+STOP | `get_tick` の連打。KAPI の中で要求が立つと syscall 入口で畳むので、`ledger_irq_ops` は増えることも増えないこともある (どちらの経路かを記録) | シェルに戻る、`kctx_irq_depth`=0・`kctx_exc_depth`=0、`used_pages` 同じ・leftover 0 |
| 対照 | `faulttest loop 3` / `faulttest kloop 3` | 3 秒で自分から終わる。`loop` は先に `get_tick` で空回りの速さを較正し (`calibrated ...` の行)、本番の空回りでは KAPI を呼ばない | `loop done after N ticks` (N ≈ 300)、rc=0、カウンタは `ledger_*_ops` 以外動かない |

`SURVIVED <kind>` が出て rc=2 なら保護が効いていない (不合格)。引数なしは使い方を出して rc=1。

**未確認の取り直し (PM、2026-10-01、17MB、`faulttest` = main `168a47b`、カーネルは `d7ac7a0` のまま)**: 起動後の基準 `fault_kill_count`=0・`ledger_exc_ops`=0・`ledger_irq_ops`=0・`used_pages`=256。

| 操作 | 結果 |
|---|---|
| `faulttest gp` (CPL=3 の `hlt`) | `[ring3] exception vec=0x0D … kill app` |
| `faulttest de` (`divl` 0 除算) | `vec=0x00 … kill app` |
| `faulttest ud` (`ud2`) | `vec=0x06 … kill app` |
| 3 本の後 | `fault_kill_count`=3、`ledger_exc_ops`=0x906 (>0)、`kctx_exc_depth`=0、`used_pages`=256、`exec_as_leftover_pages`=0 |
| `faulttest loop` (純ループ) 中に CTRL+STOP | `/api/key` は **`seq=CTRL%2BSTOP&hold=300`** で送る (MCP の `emu_key seq=CTRL+STOP` では効かなかった — 既知の罠)。EIP が 0x5002A0 (CPL=3) → カーネルへ戻り `[Process crashed]`。`ledger_irq_ops` 0 → 0x302 (IRQ 出口の `ring3_abort_check` で回収)、`kctx_irq_depth`=0、`used_pages`=256、leftover 0、`irq_ctx_violations`=1 のまま (起動時からの値)。`fault_kill_count` は 3 → 4 (CTRL+STOP も kill として数えられる) |
| `faulttest kloop` (`get_tick` 連打) 中に CTRL+STOP | `[Process crashed]`。`ledger_irq_ops` は 0x302 のまま = syscall の入口で畳まれた経路。depth 0、`used_pages`=256、leftover 0 |

残る未確認: PCM 再生中の CTRL+STOP → 再オープン・再生 (NP21/W のこの構成は `[pcm] none`、実機 Ra266 の CS4231 で)、実機 Ra266 64MB (実機エージェントに依頼中)。

### 4-3. T1c — `dma_alloc`

| 項 | 内容 |
|---|---|
| 変更 | `kernel/dma_pool.[ch]` (`dma_alloc` / `dma_free` / `dma_mark_leaked`、`limit`、`{pa, va}`、失敗時 `*out` 不変)、`drivers/pcm_cs4231.c` (呼び手)、`drivers/fdc.c` (BSS バッファの `V2P` と 16MB・64KB の検査を `dma_alloc` と同じ関数で — 置き場は変えない)、`kernel/kselftest.c` |
| ホスト試験 | `test_dma_pool.py` を拡張: `limit` 未満、64KB 非またぎ、**失敗時 `*out` 不変 (内部の `*phys_out = 0` が漏れない)**、**PCM 16KB + 82557 ≒16KB の最悪の並び** (R4)、池の番地を試験に焼かない (T3 の移動でも通る) |
| kselftest | 同じ最悪の並びを実機の池で (今の `kselftest.c:871-920` の dmap の組を置き換え)。`pa == (u32)va` の表明は `va == P2V(pa)` へ |
| NP21/W 回帰 | PCM 再生 (TASK_PCM_CS4231 の E 試験のうち NP21/W で見られるもの)、FD の読み書き (FDC の DMA) |

#### 4-3-R. T1c の実装結果 (2026-10-01、`wt/t1c`、コーダー `claude-opus-5-5`)

**大きさ** (T1b 後の残り 6.7KB に対して): `.text` 0x4cb0e → 0x4d03e (**+1,328B**)、`.data` 0x7ec3 → 0x7edb (+24B)、`.bss` 0x3e504 のまま、計 **+1,352B**。`.bss` の先頭は 4KB 整列 (0x155000) なので **`__bss_end` は 0x193504 のまま** (カーネル本体 589.3KB、ASSERT で見る残りは 6.7KB のまま)。ただし `.data` の終わりから 0x155000 までの余白が 1,565B → **229B** に減った — 次に `.text` + `.data` が 229B を超えて増えると `.bss` が 4KB 進み、残りは 2.7KB になる。

**実装したもの**: `kernel/dma_pool.[ch]` に `struct dma_buf {pa, va, size}` と `dma_alloc(size, align, limit, *out)` / `dma_free(const struct dma_buf *)` / `dma_mark_leaked(const struct dma_buf *)` (irq_save の殻)。中身は `kernel/dma_pool_math.c` の純粋関数 `dma_pool_state_alloc_buf` — **局所変数で受けて成功したときだけ `*out` を写す** (失敗時 `*out` 不変、`va = P2V(pa)`)。`dma_pool_state_alloc` に `limit` を足し、候補ごとの検査を `dma_crosses_64k` から **`dma_range_ok(addr, bytes, limit)`** (終端が `limit` 以下 + 64KB 非またぎ、`drivers/dma8237_math.c`) に替えた。旧 `dma_pool_alloc` / `dma_pool_free` / `dma_pool_mark_leaked` は撤去。呼び手: `pcm_open` は `dma_alloc(PCM_RING_BYTES, PCM_POOL_ALIGN, DMA_PHYS_LIMIT, &s_ring_buf)`、release は `dma_free` / `dma_mark_leaked(&s_ring_buf)` の後に組を 0 に。`drivers/fdc.c` は BSS の受け皿の置き場を変えず、窓の位置決め (`fdc_buf_layout`) の入力を `V2P(s_fdbuf)` に、装置へ渡す番地を `s_dma_pa = V2P(s_dma)` (3 か所の `(u32)dma_buffer` を置き換え)、窓を **`dma_range_ok(s_dma_pa, FDC_DMA_BUF_SIZE, DMA_PHYS_LIMIT)`** で検査 (落ちたら kprintf — 今と同じく言うだけ。`dma_chan_setup` が同じ条件で断るので転送は出ない)。

**ホスト試験**: `test_dma_pool.py` に 4 ケース — `range_ok` (終端 = limit は通る・1 バイト越え / 先頭だけ内側 / limit 0 / size 0 / 巨大 size / 64KB またぎ)、`limit` (limit 未満だけに置く、先頭が内側でも終端が越えれば断る、越える候補は飛ばすだけで内側の隙間に置く、断ったとき表は不変)、`keep_out` (33KB・0 バイト・整列・limit 越え・枯渇・初期化前で `*out` の 3 項とも不変、out NULL は ERR_ARG で表不変、成功は `va == P2V(pa)`・`size` は要求のまま)、`worst` (**R4**: 池の先頭を 64KB バンクの中の 16 通りの 4KB 位置 — 今の 0x2E8000 の位置も T3 の 64KB 整列も含む — に置き、前置き 3 通り (空 / 8KB 使用中 / 8KB の穴 + 次の 8KB 使用中) × 順 2 通りで PCM 16KB + 82557 16KB が入り、重ならず、前置きとも重ならず、64KB をまたがず 16MB 未満で池の中)。`pcm_cs4231_host.c` の模型を `dma_alloc` の形に (16MB を越える limit は断る)。`fdc_track_host.c` は `drivers/dma8237_math.c` を取り込む (実物の `dma_range_ok`)。`tools/check_map.yaml` の `check-fdc-track-host` に `drivers/dma8237_math.c`。
**変異**: `test_dma_pool.py --mutate` 15/15 RED (新規 6: `dma_range_ok` の 64KB 検査を外す・limit を先頭だけで見る・limit を見ない (以上 `dma8237_math.c`)、内部の出力を `*out` に直接つなぐ (Codex P3)、組の va を埋めない、呼び手の limit を捨てて 16MB で探す)。**組み立ての失敗は RED に数えない判定にした** — その結果、既存の「2 の冪でない整列を受ける」が以前は未使用関数のコンパイルエラーで RED になっていたことが分かり、`(void)dma_pool_align_ok(align);` を残す形に直して実行時の RED にした (limit を捨てる変異も `(void)limit;` を残す)。`test_pcm_cs4231.py --mutate` に 1 本 (リングを 16MB 上限で取らない → `open` が RED)。
**kselftest** (`kselftest.c` の dmap の組を置き換え): `dmap:R4 worst fits` / `dmap:R4 disjoint` / `dmap:R4 all back` (実機の池で前置き 3 通りの最悪の並び)、`dmap:fail keeps out` (33KB)、`dmap:limit refused` (limit = 池の先頭、`*out` 不変)、`dmap:0 bytes refused`、`dmap:3x16KB fit` / `free middle` / `8KB reuses` / `mid ptr refused` / `bad free cnt` / `mark_leaked ok` / `leak cnt` / `leaked no free` / `empty again`。`pa == (u32)va` の表明は `va == P2V(pa)` (`dmap_buf_ok`) へ。

**実装時の訂正** (設計と実物の食い違い):
1. **`dma_pool_state_alloc` の引数に `limit` を足した** (§3-7 は「そのまま使う、足すのは `limit` の検査と組だけ」): `limit` を確保の後で見ると「確保 → 取り消し」になり §3-2 の「全部検査してから commit」に反する。候補ごとの検査に入れれば、断った候補の表は触らない。
2. **`dma_range_ok` の置き場は `drivers/dma8237_math.c` (`dma8237.h`)**: `drivers/fdc.c` は `INC_DRIVERS` (`-Ikernel` なし) で組むので `kernel/` の関数は見えない。64KB の規則の正典 (`dma_crosses_64k`) も 8237 側にある。`dma_pool.h` は `dma8237.h` を取り込む。
3. **組を作る部分は純粋関数 `dma_pool_state_alloc_buf`** (§3-2 の表に無い): 失敗時 `*out` 不変を実物のままホストで見るため。`dma_alloc` は irq_save の殻だけ。
4. **`dma_free` / `dma_mark_leaked` は `b->pa` で span を引く** (表は物理で持つ — `base` は池の先頭の物理番地)。`b->size` は照合しない (§3-7 に規定が無い。span の先頭一致の規則はそのまま)。
5. **PCM は `s_ring` / `s_ring_phys` を残し、組 `s_ring_buf` を足した** (リングを触る既存の行を変えないため)。
6. **kselftest の `dmap:first at base` は外した**: 池の先頭の 16KB の中に 64KB 境界があれば落ちる (池の番地に依る)。82557 の量はドライバがまだ無いので定数 16KB。

**Codex 実装レビュー (2026-10-01、gpt-6-astra、Approve)**: P1/P2 なし。実装時の訂正 6 件は妥当。P3 (後続で試験を強める、未着手): (1) `pcm_cs4231_host.c` の模型が解放を `va` で引く (実物は `pa`) — `pa` だけ壊れる変更を見逃す。reset なしの通常再 open と再 open の確保失敗も足す、(2) `fdc_track_host.c` の `dma_chan_setup` の模型は物理上限を見ず、P2V/V2P が恒等なので旧キャストへ戻す変異を区別できない — FDC 経路で 16MB 拒否と V2P を観測する試験を足す、(3) `test_pcm_cs4231.py` の変異の集計がコンパイル失敗も RED に数える — `test_dma_pool.py` と同じく除外する。`dma_free` は `pa` で span を引くので、別の使用中 span の先頭や解放・再割当後の古い組を渡すとその span を解放しうる (今の呼び手に経路は無い) — 呼び手の契約として残す。

#### 4-3-N. T1c の NP21/W 回帰と、T1a〜T1b の実機 Ra266 (2026-10-01)

**NP21/W (PM、main `7d1133d` / `4233715`、17MB、PEGC)**: HDD 起動で `[selftest] 226/226 passed` (T1c の dmap の組を含む)、`kselftest_fail`=0、`[ledger] irq_ops=0 exc_ops=0 check_fail=0 bad_free=0`、起動ログに `[fdc] DMA window … crosses 64KB or 16MB` は出ない。**FD 起動** (`--fd os32_boot.d88`、カーネルと各ファイルを FDC の DMA で読む) も 226/226。**FD の DMA 書き込み → 読み戻し**: `/host/sbin/hsync.bin` (34,560 B) を FD の `/VAR/T1C.BIN` へ `cp` → `emu_reset` (キャッシュを捨てる) → FD から `/host` へ `cp` → ホストで `cmp` が一致。PCM は NP21/W のこの構成に無い (`[pcm] none`)。

**実機 Ra266 64MB (実機エージェント、CI の成果物 `737f6e4` = カーネルは `d7ac7a0` と同じ、T1a + BB 漏れの修正 + T1b。T1c は未)**: HDD 起動のままシリアル経由で更新 (手順は realhw/PLAN.md §0、CI 待ちを除いて約 20 分)。`Commit 737f6e4`・`Image CRC b2183d98 (469305 bytes, HDD loader)`、`[selftest] 225/225 passed`、`[ledger] irq_ops=0 exc_ops=0 check_fail=0 bad_free=0`、`[pcm] CS4231 v=101 irq 10 dma 1`、`[pci] 7 devices`、`mem` = Physical 65536 KB / RAM 64512 KB、`cpl0_probe: load=50002c usable_end=ef2000 cpl=0 mem_kb=65536`、`test2` ×5 すべて `PASS 5/5`、`v86 -t` OK、NOMEM・panic・停止なし。`[pegc]` の行が無いのは実機の `/etc/system.cfg` が `GFX=pc98` (`hal_test`: `backend pc98 (planar 4bpp)`) のため。→ **D32 の受入 (64MB で model 経路・起動 kselftest 0 fail) を実機で確認**。 **同日 `GFX=pegc` でも** (ユーザー指示で `gfxmode pegc` → シリアルから `reboot`): `hal_test` = `backend pegc (packed 8bpp)` 640x480、boot.log に `[pegc] hsync=24k 09a8=80 bios054c.b5=0 …` と `[pegc] gdcclk=2.5M …`、`pegcchk 3` = enter `09a8=81 clk=3 ext=1 800l=1 fifo_to=0 vs_to=0` / exit `09a8=80 clk=0 ext=0 800l=0` / `pattern drawn` / CUI へ戻る、selftest 225/225、`test2` ×3 PASS、`v86 -t` OK。9/29 (v2.1) との違い (起動時 31k・exit 81) は、**今回の実機が 24kHz で起動していた** (起動時の `09A8h` bit0 = 0、`bios054c.b5=0`) ため — exit は起動時の値へ戻す設計どおり (realhw/TASK_PEGC480_REALHW)。実機は `GFX=pegc` のまま。model の内部値 (`online` など) は実機に `/api/mem` が無いので読んでいない (kselftest の `pool:model online` が通っていることで代える)。

### 4-4. T1d — MMIO 登録と検証済み資源レコード (D33・X4)

| 項 | 内容 |
|---|---|
| 変更 | `kernel/pgalloc.[ch]` (`pgalloc_device_reserve` を `ledger_reserve_set` の核に作り直す: 資源レコードの必須化、区間の表への記録、`span_set` による完全一致、**`device_boot_map` の RESERVED / MMIO 照合を区間の表の型 (FIXED 等は拒否、BACKGROUND は許可) に置き換える**)、`kernel/sys.c` / `include/sys.h` (`sys_device_reserve_core` は `ledger_reserve_set` の薄い入口に、または撤去)、`kernel/kernel.c` (**⑥-0 `ledger_resource_import_pci()` を `pci_bind_all` の直前に**、B4)、`drivers/pci.c` (`g_pci` を読む口 `pci_for_each_mem_bar` — 列挙の順序は変えない)、`include/memmap.h` (必要なら区間の表の上限定数) |
| 仕様として残すもの (D33) | 複数 span の一括 commit、永久保持 (解除 API なし)、owner = kernel 寿命の安定 ID、同 owner・同一要求集合だけ冪等、管理範囲外も完全な範囲を記録し bitmap 操作は交差だけ、失敗時は出力も含め全不変、IRQ 保存、同種の重なる span の併合 |
| 捨てるもの | exec アリーナ全域の禁止 (`pgalloc.c:431-435` の `MEM_EXEC_LOAD_ADDR` / `legacy_ceiling` / `fixed_end` の比較) — D3 でアリーナは消える側 (T2)。T1 の間は「他 owner と重なるか」+ 「固定区間と重なるか」の照合で足りる (CPL=0 子のアリーナは `ledger_arena_top` の上にしか予約が来ないことを §3-6 が保証する) |
| ホスト試験 | **`test_device_reservation.py` を流用** (`device_reservation_stage_host.c` の実ソース ILP32 ハーネス)。X4 の 6 項目を足す: **第二 span の衝突** (1 本目は通るが 2 本目が他 owner と重なる → 全体不変)、**台帳満杯** (区間の表・資源の表のそれぞれ)、**同 owner の部分一致の拒否**、**丸めによる衝突** (バイト表記を PFN へ切り上げた結果が隣とぶつかる)、**map / probe 失敗後の永久保持** (予約後に写像を失敗させても区間が残り、再試行は完全一致で冪等)、**モジュール回収後の予約保持** (`ledger_reclaim_owner(MODULE)` が DEVICE の区間と写像を消さない)。加えて: **BACKGROUND (15〜16MB、2GB〜4GB) に重なる装置予約は通り、FIXED (カーネル帯など) に重なる予約は拒否** (P3)、**PEGC と Xe10 の実範囲 2 つを同 owner で 1 回に入れると併合されて通り、別 owner だと 2 本目が拒否される** (B3)、**span ごとの資源照合 (B10)**: gfx の 3 レコード (PEGC / Xe10-bank / Xe10-linear) と各自を指す 3 span が通り、併合後の低位区間の `res_mask` が {PEGC, Xe10-bank}・高位区間が {Xe10-linear}、同じ組の再呼び出しが冪等、span を他の資源に付け替えると拒否 (PEGC 窓を Xe10-bank で、リニア窓を Xe10-bank で)、**Xe10 を 1 レコードで表す要求 (decode 1 組) では片方の窓が decode 外で拒否** (3 回目の反例)、Trident 型 (memory BAR 3 本 + ROM、資源 4 本、1 owner) と 82557 型 (memory BAR 2 本、資源 2 本) の合成要求が通る、RAW / PROBE_UNVERIFIED のレコードは拒否、**合成 `g_pci` からの取り込み** (メモリ BAR だけ、I/O BAR と 0 は除く、溢れは `res_overflow`、RAW は予約に使えない、B4) |
| kselftest | PEGC / Xe10 の定数由来の予約が `gfx` の区間に入っていること (T1e の後)、地図検査の期待値 (MMIO 登録は sup+PCD、15〜16MB の登録の無い部分は NP) のうち**台帳から期待値を引く部分** — 3 段の地図検査そのものは T2 |
| 実機 | Ra266 で `[pci]` の行の本数 (メモリ BAR) と資源表の RAW レコード数が一致、`res_overflow == 0` (16 本で足りなければ定数を上げる) |

#### 4-4-R. T1d の実装結果 (2026-10-01、`wt/t1d`、コーダー `claude-opus-5-5`)

**大きさ** (T1c 後の残り 6.7KB、`.bss` 手前の余白 229B に対して): `.text` 0x4d03e → 0x4d0ce (**+144B**)、`.data` 0x7ed7 → 0x7ef7 (+32B)、計 **+176B** — `.got.plt` の終わりは 0x154FE4 で、`.bss` の先頭は **0x155000 のまま** (4KB 進まない)。`.bss` は旧 broker の `device_ledger` (256B) と `device_claims` の撤去で −288B (0x3e504 → 0x3e3e4)。`__bss_end` 0x193504 → **0x1933E4**、カーネル本体 589.3KB → **589.0KB (残り 7.0KB)**。**`.bss` の手前の余白は 28B** — 次の段で `.text` + `.data` が 28B 増えると `.bss` が 4KB 進み、残りは約 3.0KB になる (T1e は `sys_reserve_top` 系の撤去で減る側もある)。途中の版 (`pci_for_each_mem_bar` をコールバックで pci.c に置いた形) は +472B で `.bss` が 4KB 進み残り 3.0KB だったので、下の訂正 2 の形にして戻した。

**実装したもの**: `kernel/pgalloc.[ch]` — `struct ledger_span {first, end, kind, res}`・`LEDGER_SPAN_MMIO / RAM`・`LEDGER_MAX_SPANS` (16)・資源の bus (`PCI / CBUS / FIXED`) と width_basis (`SIZING / DATASHEET / GLUE_CONST / RAW / PROBE_UNVERIFIED`) の定数、`ledger_resource_add(rec, *rid)` (bus・根拠が既知の値、decode ⊂ [0, 4GiB]、写像範囲は空か decode の内側。満杯は `ledger_res_overflow` を数えて断る)、**`ledger_reserve_set(owner, spans, n)`** (旧 `pgalloc_device_reserve` の核を作り直し): ONLINE・`paging_boot_context()`・owner は DEVICE 種別 → **(e) span ごとに自分の資源レコードと照合 (decode の内側、width_basis が SIZING / DATASHEET / GLUE_CONST。正規化の前、B10)** → 正規化 (先頭で整列、同種の重なる・接する span を併合し根拠を `res_mask` の和に、異種の重なりは拒否) → 区間の表と照合 (FIXED / STAGING / BUNDLE / DMA / SURFACE_BACKING・他 owner の DEVICE と交差したら拒否、BACKGROUND は許可) → 同 owner が既に DEVICE 区間を持てば「正規化後の区間集合 + `res_mask` + 種別 (キャッシュ属性) が完全一致」のときだけ成功で無変更、部分一致は拒否 → 区間の表の空き → (d) 管理範囲の中は L2 に owner が無いこと (使用中と永久予約の両方)、RAM 種別は全ページ eligible で範囲の内側 → 一括 commit (eligible を落とし、DEVICE 区間を PERMANENT で記録、MMIO = UC / RAM = WB、範囲外にかかれば OUTSIDE、`span_set` は commit の通し番号)。**捨てたもの** (§4-4 のとおり): exec アリーナ全域の禁止 (`MEM_EXEC_LOAD_ADDR` / `legacy_ceiling` / `fixed_end` の比較) と `device_boot_map` の RESERVED / MMIO 照合、`SYS_DEVICE_IDLE` / `RAM_MAPPED` の capability (起動時の判定は `paging_boot_context()`、RAM の写像は ONLINE の後の eligible が恒等で張られていることで足りる)。`ledger_selfcheck` は区間の非重複の例外として **DEVICE が BACKGROUND に重なることだけ**を許す (背景は起動時の登録で重なりを断るので、重なる DEVICE より必ず前に並ぶ — 向きは 1 通り)。`region_put` (区間を 1 本足す内部関数) を `ledger_register_region` と共用。`kernel/sys.c` / `include/sys.h` — `sys_device_reserve_core` と `sys_device_span` / `sys_device_capability` / `SYS_DEVICE_*` を**撤去** (呼び手は試験だけだった)。`kernel/ledger_pci.c` (新規) — `ledger_resource_import_pci()`: `pci_get` で `g_pci` の写しを読み、メモリ BAR (Type 0 は 6 本、ブリッヂは 2 本、CardBus は 1 本、未知の型は 0 本。I/O・生値 0・予約種別・番地 0 を除く。64 ビット BAR は上位が 0 のときだけ取り、上位の BAR を飛ばす) を `width_basis = RAW`・decode 空 (`decode_first == decode_end` = BAR の先頭 PFN)・写像範囲空で載せ、載せた本数を返す。`kernel/kernel.c` — ⑥-0 として `pci_bind_all` の直前で取り込みを**独立した文で先に**済ませてから `[ledger] pci raw=N ovf=M` を 1 行出す (実機の受入の読み口。引数の中で取り込みを呼ぶと評価順が決まらず、溢れを取り込み前の値で出しうる — Codex P2)。

**ホスト試験** (`test_device_reservation.py` を書き直し、`check-memory-host` に `$(MUT)` を付けた): `test_ledger.py` の実ソース ILP32 足場 (irq_restore の度に L1 / L2 / 会計の全ページ検査) の上に、`kernel/ledger_pci.c` 丸ごと・`drivers/pci.c` から `pci_get` だけを切り出し・合成 `g_pci`。失敗時は L1・L2・会計に加えて**区間の表と資源の表をバイト列で**突き合わせる。12 本: gfx の候補群 (B3・B10 — 3 レコード / 3 span が通り、低位区間 `[0xF00, 0xF80)` の `res_mask` = {PEGC, Xe10-bank}・高位 `[0xFE000, 0xFE400)` = {Xe10-linear}、両方 PERMANENT + OUTSIDE で同じ `span_set`、付け替え 3 通り (PEGC 窓を bank で・リニア窓を bank / PEGC で) の拒否、同じ組の順不同の再呼び出しは冪等で無変更、部分一致 2 通りと根拠違いの拒否、返却・回収の拒否)、Xe10 を 1 レコードで表すと拒否、別 owner の PEGC の後の gfx の銀行窓 (B3 の反例) = **第二 span の衝突**で全体不変・他 owner との完全一致も拒否・DEVICE でない owner の拒否、FIXED / SURFACE_BACKING / STAGING / BUNDLE / DMA との交差は拒否・BACKGROUND 2 本 (15〜16MB、4GiB 端まで) は通る、Trident 型 (資源 4 本・1 owner、接する BAR は併合され `res_mask` が 2 ビット) と 82557 型 (資源 2 本、別 owner、冪等)、RAW / PROBE_UNVERIFIED・空きのレコード・範囲外の番号の拒否とレコードの検査 10 通り・**資源の表の満杯** (17 本目は `res_overflow` を数えて表は不変)、**丸めによる衝突** (バイトで接する 2 窓が PFN の切り上げで重なり 2 本目の owner が拒否)、**区間の表の満杯** (空き 1 本で 2 本の要求は全体が断られ 1 本は通る、満杯でも完全一致は冪等)、予約の冪等な再試行 (解除口が無い) と**モジュール回収後の予約保持** (MODULE の一括回収の後も DEVICE の区間と eligible の落としが残る、DEVICE の回収は拒否)。**「map / probe 失敗後の永久保持」は予約の側だけ**で、実際に map / probe を失敗させて予約と写像 (PTE) が残ることは予約の呼び手ができる **T1e の統合試験で見る** (Codex P3)、RAM 種別と使用中・永久予約・範囲外・RAM でないページ・異種の重なり、引数と時期 (初期化前・BOOTSTRAP・live AS・owner 4 通り・spans NULL・n 0 / 17・kind・decode 外 2 通り、16 本の上限は併合されて 1 区間)、**合成 `g_pci` の取り込み** (B4: I/O・0・予約種別・上位が 0 でない 64 ビット・最後の BAR の 64 ビット・ブリッヂの 18h 以降・未知のヘッダ型を除いて 4 本、各欄の値、RAW は予約に使えない、空き 1 本で 1 本だけ載り溢れ 3 を数える、PCI 無しなら 0 本) と、**`kernel.c` の ⑥-0 の塊を実物のまま切り出して走らせる表示の経路** (溢れた状態で `raw=1 ovf=3` が渡る、Codex P2)。段の試験 (`device_reservation_stage_host.c`、実 paging + pgalloc + sys) は RAM + 窓の一括予約を `ledger_reserve_set` (gfx、DATASHEET) で組み直した (BOOTSTRAP で拒否、窓の最後のページが使用中なら全体不変、解放後に通り冪等、写像は変わらない、`ledger_selfcheck`)。テキスト検査 2 本: 旧 broker の名前が残っていない、⑥-0 は `memory_boot_init` の後・`pci_bind_all` の前。
**変異**: `test_device_reservation.py --mutate` **24/24 RED** (全部実行時の検出、組み立ての失敗は RED に数えない): RAW に予約権限、decode の終端 / 先頭を見ない、`res_mask` を併合しない、異種の重なりを併合、BACKGROUND を拒否、FIXED を許可、他 owner の DEVICE を許可、部分一致を受ける、冪等の判定で `res_mask` を見ない、L2 の owner を見ない、RAM の eligible を見ない、eligible を落とさない、DEVICE でない owner を受ける、selfcheck が DEVICE と BACKGROUND の重なりを咎める、溢れを数えない、写像範囲の逆転を受ける、取り込みを検証済みにする・I/O BAR を取る・64 ビットの上位の判定を逆にする・ブリッヂの 6 本を読む・未知のヘッダ型の BAR を読む・溢れを載せた数に数える・表示が取り込みの前の溢れを出す。

**kselftest**: T1d では足していない — §4-4 の kselftest 欄は「PEGC / Xe10 の定数由来の予約が gfx の区間に入っていること (T1e の後)」で、T1d の時点では起動時に DEVICE 区間を作る呼び手が無い (予約は ⑥ の gfx = T1e)。地図検査の期待値を台帳から引く部分も、引く DEVICE 区間が無いので T1e で入れる。⑥-0 の取り込みは ⑤ (`kselftest_run`) より後なので kselftest からは見えず、`[ledger] pci raw= ovf=` の行で読む。

**実装時の訂正** (設計と実物の食い違い):
1. **`sys_device_reserve_core` は薄い入口にせず撤去した** (§4-4 の「または撤去」): 呼び手は試験だけで、capability (IDLE / RAM_MAPPED) は `paging_boot_context()` (master CR3・live AS 0) と ONLINE 後の恒等写像で置き換えた。**留保** (Codex P3): 旧 IDLE が表明していた「CPL=0 の exec が生きていない」までは `paging_boot_context()` は検査しない — T1 の予約の呼び手は起動時の ⑥ (exec_init より前) だけなので実害は無いが、同義ではない。`fixed_end` (sys の低位固定域) も捨てるもの (§4-4) の一部。
2. **`pci_for_each_mem_bar` は足さず、`ledger_resource_import_pci` を `kernel/ledger_pci.c` に置いて既存の読み口 `pci_get` (写しを返す) で `g_pci` を読んだ** (§4-4 の「`drivers/pci.c` (`g_pci` を読む口 `pci_for_each_mem_bar`)」): pci.c にコールバック式の口を置いた版は +472B で `.bss` が 4KB 進んだ (残り 3.0KB)。`pci_get` なら pci.c は無変更、列挙の順序も config も触らない。取り込みを `pgalloc.c` でなく別ファイルにしたのは、`pci.h` (`os32_kapi_shared.h` を引く) を `pgalloc.c` に入れると pgalloc.c を取り込む 9 本のホスト試験の探索パスを全部広げることになるため。
3. **メモリ BAR の数え方** (§3-3 ⑥-0 は「メモリ種別で非 0 のもの」だけ): ブリッヂ (Type 1) の 18h 以降はバス番号と窓の設定で BAR ではないので、ヘッダの型で BAR の本数を Type 0 = 6 / ブリッヂ = 2 / CardBus = 1 / **未知の型 = 0** に限った (初版は未知の型を 1 本扱いにしていた — Codex P3 で既知の型だけに。三項の連鎖は +28B で `.bss` が 4KB 進むので、型ごとの本数の 3B の表で引く)。64 ビット BAR は上位が 0 のときだけ取り上位の BAR を飛ばす、予約種別 (bit2〜1 = 11b) は除く。**`[pci]` の行 (`pci_report`) はこの区別をしない**ので、ブリッヂがある機械では `[pci]` の mem の本数と `raw=` が食い違いうる (Ra266 の既知の列挙 §2-2 の 16. ではブリッヂの mem 表示は無い)。
4. **Ra266 の期待値は 5 本** (§2-2 の 16. の「PCI 採取値 6 本」は Expansion ROM を含む): `g_pci` は ROM (config 30h) を読まないので取り込みは memory BAR 5 本。ROM を資源にするのは sizing を行う段 (T4 以降)。
5. **`revision` と `boot_gen` は 0**: `g_pci` に revision の欄が無い (`struct pci_dev` は 40B で KAPI の写しと固定)。`boot_gen` は再確認 (P4) の段で意味を決める。RAW は予約権限にならないので T1 では参照しない。
6. **RAW の decode は空** (`decode_first == decode_end` = BAR の先頭 PFN): 幅が未確定なので、万一 width_basis の検査が抜けても包含検査で落ちる。
7. **RAM 種別の span の L2**: `ledger_reserve_set` は旧 broker と同じく eligible を落とすだけで L2 に owner を書かない (DEVICE owner の `pages` は 0 のまま、§3-1 の「DEVICE は RAM を持てない」と整合)。区間の表 (DEVICE・WB) が持ち主を持つ。T1 の呼び手に RAM 種別は無い (BB は T1e で `pgalloc_alloc_n_owner`)。
8. **使用中の検査は L2 だけで行う**: allocated ⇔ owner ≠ 0 (eligible なページ) と a ⇒ e から、`owner_map[p] ≠ 0` が「使用中」と「永久予約 (`pgalloc_reserve_pfn`)」の両方を覆う。旧 `device_boot_map` の RESERVED 照合が拾っていた永久予約はここで拾う。
9. **冪等の判定は「その owner の DEVICE 区間の全部」と比べる**: 同 owner は部分一致を拒否するので owner あたりの集合は常に 1 つで、`span_set` は記録するが判定には使わない。
10. **`check_map.yaml`**: `kernel.c` が `pgalloc.h` を引くようになったので、`kernel.c` を取り込む 3 検査 (bootlog / packages / sh-truncation) に `pgalloc.h` と `physmem.h`、`check-memory-host` に `kernel/ledger_pci.c`・`drivers/pci.c`・`drivers/pci_decode.c` と (`pci.c` 経由の) `gfx/gfx.h`・`gfx/palette.h` を足した (`make check-map` の指摘どおり)。

**Codex 実装レビュー (2026-10-01、gpt-6-astra、Request changes)**: P2 1 件 — `[ledger] pci raw= ovf=` の `kprintf` の引数の中で取り込みを呼んでいたので評価順が決まらず、当時の `kernel.o` は `ledger_res_overflow` を先に読んでいた (メモリ BAR 17 本で `raw=16 ovf=0` と出る)。→ 取り込みを独立した文にし、表示の経路をホスト試験で実物のまま通した (上)。P3 3 件 — 試験の説明の「map / probe 失敗後」を予約の側に限り T1e へ回す、訂正 1 の留保、未知のヘッダ型を 0 本に (いずれも上に反映)。予約の核に他の blocker は無し。**大きさ**: 直しの後も `.text` 0x4d0ce・`.data` 0x7ef7 のまま (表 3B は `.data` の詰め物に入った)、`.bss` 手前の余白 28B・残り 7.0KB は不変。

**PM が確かめること**: NP21/W (17MB / 8MB、PEGC と Cirrus) で起動 kselftest 0 fail、`[ledger] pci raw=0 ovf=0` (PCI 無し)、`ledger_check_fail` = 0、`ledger_region_count` が T1c と同じ (起動時の DEVICE 区間は無い)、GUI 起動と `v86 -t` が今までどおり (T1d は起動時の予約・写像の順を変えない)。実機 Ra266 (ログだけ): `[pci]` の行と並んで `[ledger] pci raw=5 ovf=0` (Trident 3 + 82557 2。ROM は数えない)、起動 kselftest 0 fail。

### 4-5. T1e — gfx の識別・予約・写像、BB の SURFACE、`sys_reserve_top` の撤去

| 項 | 内容 |
|---|---|
| 変更 | `kernel/kernel.c` (⑥ の新設と `GFX=` 読み取りの前倒し、⑨ の移譲、検査地点 `ledger_selfcheck("gfx")` / `("gui")`)、`gfx/gfx_core.c` (識別の口 `gfx_identify_candidates` — 副作用なし、`gfx_bb_phys_range` を「選択中 backend の CLIENT の SURFACE」から、`fb->planes[0]` の `bb_base` は SURFACE から引いた仮想)、`gfx/backend_pegc.c` (probe から map と BB 確保を外へ、`pegc_reserve_backbuffer` は受け取るだけ)、`gfx/backend_cirrus.c` (map を probe の前へ、失敗しても予約・写像は保持、`:327-329` の古いコメントの撤去、**CLIENT / DISPLAY の SURFACE を ⑥ で登録し `:380-381` の `bb_base/bb_size` は SURFACE から** — B9)、`kernel/sys.[ch]` (暫定 `sys_reserve_top(owner, …)` と `sys_top_reserved` の撤去、`sys_usable_mem_end` は §3-6)、`kernel/pgalloc.c` (SURFACE の表)、古い記述の掃除 (`memmap.h:158`、`backend_pegc.c:685`、`CLAUDE.md` の Memory layout 表の hot-deploy 2 行 — 正典の `02_memory.md` §2-1 は生成) |
| ホスト試験 | 識別 → 予約 → 写像 → probe の順を mock の I/O / 写像トレースで (DEVICE_RESERVATION §7 T3 の形を流用): PEGC の map 失敗・BB 不足、Cirrus の map 失敗・probe 失敗で、予約は保持・BB は公開されない・PC98 へ落ちる。**BB の量 = 候補の最大** (auto / pegc / cirrus / pc98 × PEGC 可 / 不可 の表)。**auto で Xe10 の probe が失敗して PEGC に落ちるとき、PEGC の窓が (先に入れた Xe10 の予約に阻まれず) 使える** (B3、統合試験は実範囲 2 つ・資源レコード 3 本 — PEGC / Xe10-bank / Xe10-linear、B10)。**BB の探索範囲がアリーナ内で、17MB / 64MB でも今の区間になる** (X14)。**`ledger_arena_top()`: 永続確保が無ければ `pgalloc_arena_end()`** |
| kselftest | 選択中の backend の CLIENT の SURFACE が表にあり owner = boot (GUI 後は gshell)、planar の固定 SURFACE が 1 本、Cirrus 候補なら CLIENT + DISPLAY の 2 本 (MMIO backing、UC)、`gfx_bb_phys_range()` の範囲が SURFACE と一致、`sys_usable_mem_end() <= ledger_arena_top()`、`ledger_check_fail == 0` (3 地点) |
| NP21/W 回帰 | **3 バックエンド** (planar = `GFX=pc98`、PEGC、Cirrus = `cirrus-on`) で GUI 起動・描画・present、**`cirrus-on` で CPL=3 の GUI 描画アプリ (gdi_test) がクライアント面 0xFE04B000 へ描けて #PF 0 件** (B9、`fault_kill_count` 不変)、**GUI → CUI → GUI で BB の物理番地が同じ** (R5 (b))、**BB の物理番地が T1e の前 (`sys_reserve_top` の結果) と同じ** (X14)、Cirrus の probe を失敗させる構成 (`cirrus-off` + `GFX=cirrus`) で予約と写像が残り PC98 で起動、**`cirrus-off` + `GFX=auto` で PEGC に落ちる** (B3)、8MB × PEGC で GUI (T1a と同じ構成で再確認)、`ledger_check_fail == 0` を `/api/mem` で読む |

### 4-6. T1f — P2V / V2P の全面適用と検査

| 項 | 内容 |
|---|---|
| 変更 | §2-3 の 161 件 (T1b〜T1e で書き換え済みの分を除く。1 件ずつ分類する)、静的初期化子 10 か所 + マクロ 2 か所を `P2V_CONST` / `P2V_IO_CONST` に (B6)、`exec.c:816-829` の表歩きを `paging.c` の `as_va_to_pa` 系へ、`paging_verify_identity` と kselftest の恒等表明の書き直し、`tools/check_p2v.py` + `tools/check_p2v_allow.txt` + `build/sdk.mk` (`check-p2v` を `check` と `check-changed` へ)、`docs/CONSTRAINTS.md` と `CLAUDE.md` (新 ID、同じコミットで — `check_constraints.py` が整合を見る) |
| 受入の芯 | **コード列の同一は補助証拠** (Codex P3): 恒等なので、書き換えの前後で `objdump -d` の `.text` が一致すること (差が出たら理由を列挙 — `static inline` の展開順や定数の畳み方) に加えて、(a) **静的初期化子は `objdump -s -j .data` の初期値が一致**、(b) **表歩きの移動 (`as_va_to_pa`) はホスト試験で USER・RW の判定を 4 通り** (P2V 化とは別に検証する)。NP21/W の回帰は軽くてよい |
| 検査 | `make check-p2v` が 0 件 (**アプリ帯・lease 窓の番地に `V2P` を当てた箇所も 0、関数の中の `P2V_CONST` も 0**、§3-4)。変異: 4 形の違反の注入でそれぞれ落ちる。`make check` と `make check-changed` の両方から到達する (X16) |
| 対象外の扱い | V86 のゲスト線形・ページ 0 の二重の意味 (`v86_bios.c:283,1207` の `guest = (u8 *)0` はゲスト線形であり物理でもある) は例外一覧に理由付きで。HostDrv の 24 件は §5 T1-U6 の確認まで例外一覧 (「NP21/W が線形で読むか物理で読むか未確認」) |
| NP21/W 回帰 | 17MB で起動 kselftest 0 fail、GUI・CUI・V86・FD・HostDrv の一巡 |

### 4-7. T1 全体の受入 (TASK_MEMMAP_V3 §6 T1 の受入の要点との対応)

| T1 の受入の要点 (§6) | 段 |
|---|---|
| kselftest: 予約・割当・解放・失敗時回収 (モジュール init の途中失敗を含む) | T1b |
| 64KB 境界 (PCM リング + 82557 の同居) | T1c |
| 8MB / 17MB / 64MB のすべてでモデル経路 (legacy fallback 撤去、D32)。**8MB で exec の有効範囲が残る** (B2) | T1a |
| 高位 RAM の登録源は `memory_boot_detect` (0594h + 書き込み検証) だけ | T1a |
| MMIO 登録の一括 commit と失敗時の全不変 (流用試験) | T1d |
| X4: 第二 span の衝突、台帳満杯、同 owner の部分一致の拒否、丸めによる衝突、map / probe 失敗後の永久保持、モジュール回収後の予約保持。**候補群の重なり (PEGC + Xe10) と背景との区別** (B3、P3) | T1d / T1e |
| P2V 検査 0 件 (アプリ帯・lease 窓の番地に `V2P` を当てた箇所も 0) | T1f |
| 割り込み中の台帳操作の件数を報告 (**例外は全ベクタ、PCM の再オープン**、B1・B7) | T1b |

### 4-8. 段の境目で既存の予約関数が誰の owner で呼ばれるか (Codex B11 の一般形)

各段を単独で着地させたとき、その段の終わりに残っている予約・確保の口と、渡る owner。**空欄 (owner を取らない口) が残る段があってはならない** — T1b で旧 API を消すので、T1b 以降は**池 (L1 / L2 の管理対象) の口**が全部 owner 付き。表の読み方 (Codex 3 回目 P3):

- **DMA プール (`dma_pool_alloc` / `dma_alloc`) は owner の対象外** — 池の外の固定 DMA 区間 (L3 の DMA 型、区間の owner は kernel) の内側を span 表で配るだけで、ページごとの L2 を持たない (§3-7、R4)。
- **workspace (`pgalloc_alloc_pt` / `_free_pt`) は固定の kernel owner** — 呼び手が owner を選ぶ口ではない (workspace は eligible でないので L2 の会計には入らず、区間の表の FIXED (owner = kernel) で持つ)。
- **T1f の列の `—` は「前段 (T1e の終わり) のまま引き継ぐ」** の意味で、空欄ではない (T1f は P2V / V2P の書き換えだけで owner を変えない)。


| 口 (今の関数) | 呼び手 | T1a の終わり | T1b の終わり | T1c / T1d | T1e の終わり | T1f |
|---|---|---|---|---|---|---|
| `sys_reserve_top` → `pgalloc_reserve_pfn` (永久予約) | `pegc_reserve_backbuffer` (`backend_pegc.c:690`) | 今のまま (owner 無し、L2 は無い) | **`sys_reserve_top(boot, bytes)`** — L2 = boot、eligible を落とす。直後の `pgalloc_mark_used` (`:698`) は消す (今も無操作) | 同左 | **撤去**。BB は `pgalloc_alloc_n_owner(boot, …, TOP_DOWN)` + SURFACE | — |
| `pgalloc_mark_used` (固定番地 claim) | `shlib_init` (`shlib.c:95`) | 今のまま | `ledger_claim_fixed(shlib, …)`、失敗経路 11 か所は `pgalloc_free_n_owner(shlib, …)` | 同左 | 同左 | — |
| 同上 | `exec_cpl0_claim` (`exec.c:988-989`) | 今のまま | `ledger_claim_fixed(cpl0 暫定 owner, …)`、release は `pgalloc_free_n_owner`。入口判定は不変 (§3-6) | 同左 | 同左 (`sys_usable_mem_end` = `min(frozen, ledger_arena_top)`) | T2 で撤去 |
| `pgalloc_alloc_page` (AS の PD / PT) | `paging.c:706-709` | 今のまま | `pgalloc_alloc_n_owner(AS, 1, …)` — AS owner は `exec` が AS 作成の直前に取り `struct addrspace` に持つ | 同左 | 同左 | — |
| `pgalloc_alloc_n` / `_page` (アプリの私有ページ) | `app_map_region` (`exec.c:1090,1101`) | 今のまま | owner = AS | 同左 | 同左 | — |
| `pgalloc_alloc_n` (shlib data 複製) | `shlib.c:278` | 今のまま | owner = attach する AS (`:311` の解放も) | 同左 | 同左 | — |
| `pgalloc_alloc_n(160)` (V86 バッキング) | `v86_mem.c:44` | 今のまま | `v86_mem_setup(owner, …)`: KAPI 経由なら呼び手の AS、カーネル内の利用者があれば kernel (今の呼び手は `v86` = CPL=3 アプリだけ) | 同左 | 同左 | — |
| `pgalloc_alloc_pt` / `_free_pt` (workspace) | `reserve_table` (`paging.c:400,442`) | legacy 分岐を撤去 (全構成が workspace 持ち) | L2 は kernel (workspace は eligible でない) | 同左 | 同左 | — |
| `paging_addrspace_create_n` + `pgalloc_alloc_n(2)` (paging の自己試験 `paging_app_band_selftest`) | `paging.c:1082,1127` (試験用 AS の作成)、`:1144` (2 ページ)、`:1165` (`paging_addrspace_free_user_range` で AS 経由の解放) | 今のまま | **試験用 AS の owner を明示的に取る** (`ledger_owner_new(AS, 試験用 id, "pgtest", …)`)。AS の PD / PT も 2 ページも**その owner で確保** (`pgalloc_alloc_n_owner(as.owner, 2, …)`) し、`:1165` の AS 経由の解放 (`pgalloc_free_n_owner(as->owner, …)`) と一致させる — 写像だけでは L2 の owner は移らないので、kernel で確保すると解放が拒否され空きページ数の比較 (`:1169`) が落ちる (Codex 3 回目 B12)。解放の owner 検査は緩めない。試験の最後に `ledger_owner_pages == 0` を見て `ledger_owner_retire`、失敗枝は `ledger_reclaim_owner` で掃除してから retire (rc のビットは立てたまま)。`:1082` の AS も同じ形 | 同左 | 同左 | — |
| `pgalloc_device_reserve` (装置予約) | 試験だけ (`sys.h:14`) | 今のまま | 今のまま (呼び手なし) | **`ledger_reserve_set` に作り直す** (T1d)。呼び手はまだ試験だけ | ⑥ の `gfx` (§3-8) | — |
| `paging_map_phys` (窓の写像) | `pegc_probe` (`backend_pegc.c:586`)、`cirrus` の init (`:335`) | 今のまま (台帳は窓を知らない — RAM でないので害はない) | 同左 | 同左 | ⑥ へ (予約 → 写像 → probe) | — |
| `dma_pool_alloc` | `pcm_open` (`pcm_cs4231.c:525`) | 今のまま | 今のまま (DMA プールは池の外、owner 無し) | `dma_alloc` (T1c) | 同左 | — |

---

## 5. リスクと未確認

### 5-1. リスク

| # | リスク | 手当て |
|---|---|---|
| T1-R1 | **カーネル本体の残り予算 13.0KB** に台帳のコードと kselftest の追加が入らない (T3 はまだ)。固定表 ≈3.0KB は BSS (§3-1) | `legacy_metadata` / `pgalloc_init` の撤去で相殺 (−1KB)、`device_boot_map` は動かさない、段ごとに増分を測る (T1a で減り、T1b で増える)。入らなければ T3 の「固定 PT を画像の外へ」(BSS の `pd_raw` + `pt_raw` + `aperture_pt_raw` ≈ 40KB) だけを T1 の前へ切り出す (§6 X15 — その場合は PT の予約・写像・初期化とリンク ASSERT / 地図検査を一組で動かす) |
| T1-R2 | 8MB 型の置き場 8KB に metadata が入らない (L2 を広げたとき) | 固定表は BSS なので metadata は L1 + L2 だけ。`STATIC_ASSERT(pgalloc_metadata_bytes(3,073 PFN) <= PAGE_SIZE)`。溢れる設計変更 (owner を u16 にする等) は T3 で KERNEL_SLACK へ移してから |
| T1-R3 | 深さの変数を全 IRQ スタブ・全例外スタブに入れる asm の変更が、V86 の反射・longjmp と噛み合わず深さが漏れる | 控えを jmpbuf に入れて `exec_longjmp` 1 か所で戻す (§3-5)。深さの漏れを kselftest と `mem` で見る (通常文脈で 0 であること)、変異 (`IRQ_LEAVE` / 例外の −1 / 復元 を抜く) で試験が止まることを確かめる |
| T1-R4 | ⑥ で `GFX=` の読み取りを `exec_init` の前へ動かしたとき、`sysconfig_get_str` が `exec_init` 後の何かに依存している | **静的に解消**: `vfs_read` だけに依存 (§2-2 の 12.)。T1e の最初に実機でも確かめる |
| T1-R5 | Cirrus の map を probe の前へ動かすと、今「最初の init で 1 回だけ張る」前提 (`backend_cirrus.c:320-331`) と二重に張る | 張るのは ⑥ の 1 回だけにし、backend の init からは map を消す。単独 CPL=3 アプリの `gfx_init` で PTE を supervisor で上書きする旧障害 (2026-09-06) が戻らないことを回帰で見る |
| T1-R6 | CPL=0 子の claim を owner 付きにしたとき、入口判定 (`appslot_cpl0_admit`) が通した起動を claim が拒む場面が出る (設計上は無いはず: 生存アプリ 0 本・GUI 外) | claim の拒否は `claim_refused` と `EXEC_ERR_NOMEM` で見える形にし、入口判定とエラーコードは変えない (§3-6、Codex 2 回目 P3 で 1 回目の「挙動の変化」の記述を撤回)。受入は `cpl0_probe` (CUI から通る、GUI からは入口で `OS32_ERR_INVAL`)。T2 で claim ごと消える |
| T1-R7 | 資源の表 16 本が Ra266 のメモリ BAR で足りない | Ra266 の列挙は memory BAR 5 本 + ROM 1 本、gfx の候補群 3 本 (PEGC / Xe10-bank / Xe10-linear) と合わせて 9 本 (§2-2 の 16.) なので入る見込み。`res_overflow` を数え、実機の `[pci]` の行で本数を確かめる (T1d の実機) |
| T1-R8 | `KSETJMP_BUF_LEN` を広げたとき、長さを直書きした保存先が残る | 3 か所を列挙して統一 (§3-5)。`grep 'jmpbuf\[[0-9]'` を `check-changed` の検査に足す (B8) |
| T1-R9 | Xe10 の decode 4MB / 写像 2MB は NP21/W の窓の定数で、実機 Xe10 では違うかもしれない | レコードに GLUE_CONST (NP21/W 由来) と明記。実機の幅は P4 の実測 BAR と同じ扱い (T1 では実機の Xe10 を試験しない) |

### 5-2. 未確認 (TASK_MEMMAP_V3 の U 番号との対応 + この票の分)

| # | 前提 / 測るもの | 対応 | どこで |
|---|---|---|---|
| U1 | owner / lease 台帳の実サイズ (16KB は推測) | 固定表 ≈3.0KB (BSS) + 1.25B/PFN (metadata) の見込み (§3-1) を実測で置き換える | T1b |
| U10 | PCI の BAR が 0x80000000 以上にある機種 | D11 で RAM の上限を 2GB にした後、`[2GB, 4GB)` の BAR は BACKGROUND の上の DEVICE 区間 + 資源レコードで持つ | T1a / T1d |
| U24 | 0594h の意味を機種資料と照合 | T1a の作業に入れた | T1a |
| U19 | `paging_init` / 台帳をルートマウントの前へ | **T1 では触らない** (T5b)。T1 の台帳はルートマウントの後に作られるので、T5b は ③ を前へ動かすだけで済む形にする (③ が VFS に依存しないこと) | T5b |
| T1-U1 | 8MB 機でアプリの identity 写像が `[0x2F9000, 0x2FB000)` の表に届かないこと (PDE 0 はカーネル帯で supervisor) | kselftest の MM 検査 (`MM_RW` vs `MM_RWU`) に置き場を足す | T1a |
| T1-U2 | V86 バッキングの解放 (`v86_mem_teardown`) が IRQ フレームの中で走るか | **静的には「走らない」** (teardown は `v86_run` の setjmp 復帰後、§2-1)。R1 の計数で 0 を実測して閉じる | T1b |
| T1-U3 | 例外フレームの中の回収 (fault kill、全ベクタ) を R1 と同じ扱いにするか | 数えて報告 (§3-5)。扱いは T2 で決める (Codex X12) | T1b → T2 |
| T1-U4 | Xe10 に副作用のない識別手段が無い — 予約を `GFX=` と機種だけで決めてよいか | 予約は存在の証明ではない (DEVICE_RESERVATION §6)。候補群 `gfx` の予約 (§3-8)。予約した窓に RAM 登録が無いことだけ確かめる | T1e |
| T1-U5 | `paging_map_phys` の失敗時の契約 | **解消** (§2-2 の 7.): 全件不変。`backend_cirrus.c:327-329` のコメントが古い。T1e で直す | T1e |
| T1-U6 | HostDrv の hypercall (`fs/hostdrvfs.c`) が渡すポインタを NP21/W が線形番地で読むか物理番地で読むか | NP21/W ai-debug フォーク (`~/np21w-src`) の HostDrv 実装で確かめる。T1f では例外一覧に置く | T1f |
| T1-U7 | 文字列検査 (`check_p2v.py`) の取りこぼし率 | 変異試験で 4 形を測る。`-Wcast-align=strict` のようなコンパイラ側の検出手段は無い (恒等なので型が同じ) | T1f |
| T1-U8 | (撤回 — Codex 2 回目 P3: GUI からの `--cpl0` は `appslot_cpl0_admit` が入口で `OS32_ERR_INVAL` にしている、試験 20A〜20E) | — | — |
| T1-U9 | 実機 Xe10 のリニア窓の decode 幅・銀行窓の位置 (今の定数は NP21/W の窓) | T1 では扱わない (T1-R9)。P4 の実測 BAR と一緒に | P4 |

---

## 6. Codex に突き合わせる論点 (X9〜)

### 6-1. 論点と票の案 (1 回目に出したもの。判定は §6-2)

TASK_MEMMAP_V3 §8-4 の X1〜X8 の続き。T1a の着手前に突き合わせる (ROLES §5 の書式、`codex exec -s read-only`)。

| # | 論点 | この票の案 |
|---|---|---|
| X9 | `MEM_POOL_BASE` の値を T1 で 0x500000 にするか、今の `PGALLOC_BASE` (0x400000) のまま名前だけ置き換えて T3 で 0x500000 にするか | 後者 (§4-1)。T1 では 0x400000〜0x4FFFFF が shlib 帯で、shlib が無いときは池へ返す今の挙動を保つため。0x500000 にすると shlib の無い構成で 1MB が死ぬ |
| X10 | 8MB 型の metadata の置き場を DMA プールの上の予約域にする案 — アプリ帯の外・固定番地・BSS 非増加。別案 (a) KHEAP から取る、(b) BSS に 16MB 分を静的に、(c) 池の中 (アプリ帯の identity と重なるが owner = kernel で配らない) | 予約域案 (1 回目は 12KB、**改訂で 8KB `[0x2F9000, 0x2FB000)` + 4KB ガード**、固定表は BSS)。(c) は APP_BAND_PDE §2 の不変条件 (2 枚 PDE のアプリが identity を USER で写す) を T2 の前に崩すので不採用 |
| X11 | L2 (owner) を allocated bitmap と**別に持つ** (不変条件 allocated ⇔ owner ≠ 0) か、bitmap を owner 配列で置き換えるか | 別に持つ (既存のホスト試験のハーネスが bitmap の不変条件を見ているので、T1 の差分を小さくする)。一本化は T3 以降の掃除 |
| X12 | R1 の計数に**例外フレームの中の回収** (fault kill) を含めるか、IRQ と分けて数えるか | 分けて数える (`ledger_exc_ops`)。R1 の文面は ISR だけを名指ししているが、同じ形の反例 (回収の中の待ち) は例外経路にもある |
| X13 | 深さの計数を全 IRQ スタブに入れる (asm 変更) か、台帳の mutator の入口で「IF=0 かつ起動列でない」を代理にするか | 前者。代理は `irq_save` 区間の中からの呼び出し (paging の内部) を誤検出する |
| X14 | T1 の間の `ledger_arena_top` (CPL=0 子のアリーナ上端を台帳から求める暫定) で、`sys_reserve_top` の撤去と CPL=0 子の併存が安全か | §3-6 の 2 点 (BB はアリーナ内 TOP_DOWN で今と同じ区間、claim は他 owner 混在を拒否) で足りるか |
| X15 | カーネル本体の予算 13KB が足りないとき、T3 の「固定 PT を画像の外へ」だけを T1 の前へ切り出してよいか (順序 T0 → T1 → T2 → T3 の例外) | 必要になったときだけ (T1b の実測で判断)。決定 (順序) を変えるので、その場合はユーザーに上げる |
| X16 | P2V の検査を `check_constraints.py` ではなく新しい `check_p2v.py` に置く — TASK_MEMMAP_V3 §3-4 の「`check_constraints.py` に検査を足す」の読み方 | ID は `CONSTRAINTS.md` に足して `check_constraints.py` が整合を見る、走査は `check_p2v.py` (既存の `check_le_access.py` と同じ形)。`check_constraints.py` は ID の整合だけの道具なので、走査を混ぜると役が 2 つになる |

### 6-2. Codex レビュー 1 回目 (gpt-6-astra、2026-09-30、`wt/t1-design` `96decf6`) の所見と対応

判定は **Request changes**。D1〜D36 の変更は求められていない。所見の全文は PM の作業域 (この票には要旨と対応だけ)。

| # | 所見 (要旨) | 採否 | 対応 (この票の場所) |
|---|---|---|---|
| X9 | 採る。固定 shlib 帯を ③ で永久除外せず、⑧ の claim / 失敗時返却を維持 | 採る | §3-3 ⑧ に明記 |
| X10 | 条件付き (B2)。容量は実サイズで再計算 | 採る | §3-1 (実サイズ)、§3-3 (8KB + ガード、検証経路) |
| X11 | 採る。同じ IRQ 保存区間で bitmap と owner を更新し失敗で両方戻す。workspace・永久予約の位置を明記 | 採る | §3-1 L2 の行 |
| X12 | 条件付き (B7) | 採る | §3-5 (全例外の入口) |
| X13 | 条件付き (B1)。IF=0 は代理にならない。既存 `irq_in_irq` の拒否判定は置き換えない | 採る | §3-5、§1-2 (broker の判定は T2) |
| X14 | 条件付き。「TOP_DOWN なら今と同じ区間」は不成立 (全池なら高位 RAM 側)。探索範囲・永続確保が無い場合の戻り値・claim 失敗時にロードを始めないこと | 採る | §3-6 (探索範囲 = アリーナ内、戻り値、拒否は ロード前)、T1e の回帰 (BB の番地が前と同じ) |
| X15 | 採る。前倒し時は PT の予約・写像・初期化とリンク / 地図検査を一組に | 採る | §5-1 T1-R1 |
| X16 | 採る。通常チェックと変更時チェックの両方から到達することを確認 | 採る | §3-4 検査、T1f |
| **B1** (P1) | `irq_in_irq` は broker の `IRQ_ERR_CTX` 判定に使われる。全 IRQ の深さに置き換えると PCM の CTRL+STOP 回収で `irq_unregister` が断られ、次の `pcm_open` が重複登録で失敗 | **採る** | §2-2 の 3.、§3-5: 観測用 `kctx_irq_depth` を新設し `irq_in_irq` と broker の判定は触らない。受入に「CTRL+STOP 後の PCM 再オープン・再生」と「`irq_ctx_violations` 不変」(T1b) |
| **B2** (P1) | 8MB の低位 backing は `physmem_reserve_ram` / `init_model` の検査で ③ が失敗し、直しても `sys_frozen_exec = workspace_first` で exec 上端が 3MB になる | **採る** | §2-2 の 2.、§3-3: backing の種類 ARENA_TOP / FIXED で検証経路を分け、exec 上端は `pgalloc_arena_end()` (backing の位置から独立)。受入に 8MB の exec 有効範囲 (`sys_usable_mem_end() == 0x800000`、CPL=3 アプリと `--cpl0` の起動) (T1a) |
| **B3** (P2) | auto の候補予約で PEGC `[0xF00000, 0xF80000)` と Xe10 銀行窓 `[0xF60000, 0xF68000)` が重なり、別 owner だと後者が拒否され fallback も妨げる | **採る** | §3-8: 候補群を owner = DEVICE `gfx` の 1 回の一括 commit にし、既存の核の併合で 1 区間に。資源レコードは候補ごと。実範囲 2 つの統合試験 (T1d / T1e) |
| **B4** (P2) | `pci_init` (`kernel.c:370`) は `memory_boot_init` (`:557`) より前 — 列挙時に資源レコードを書けない | **採る** | §2-2 の 9.、§3-3 ⑥-0: `g_pci` の生値を ③ の後 (`pci_bind_all` の直前) に `width_basis = RAW` (採取値、予約権限なし) で取り込む。検証済みレコードと区別 (T1d) |
| **B5** (P2) | T1a 単独着地で 8MB が 1KB の `legacy_metadata` に L2 + 固定表を書く | **採る** | §4: **段の順を入れ替え** (T1a = モデル経路の一本化、T1b = 台帳の核)。8MB の回帰は両段で。固定表は BSS (§3-1) なので metadata は L1 + L2 だけ |
| **B6** (P2) | 静的初期化子 (`gfx_core.c:17`、`kcg.c:52-55`、`utf8.c:32`、`backend_pc98.c:154`) は `static inline` に置換できない。`uptr` は未定義 | **採る** | §2-2 の 10.、§3-4: 定数式用の `P2V_CONST` / `P2V_IO_CONST` (関数内では違反)、`uptr` を `types.h` に。対象 10 + 2 か所を列挙。`.data` の初期値比較を受入に (T1f) |
| **B7** (P2) | `exception_handler` は全ベクタを `ring3_fault_kill` に流す。#PF / #GP だけでは #DE などが漏れる | **採る** | §2-2 の 4.、§3-5: 全例外スタブの入口で数え、復帰する経路と longjmp する経路の両方で戻す (控えは jmpbuf)。受入に #DE か #UD (T1b) |
| P3 | 12KB の容量を実サイズで再計算 (`device_boot_map` 1,032B、owner 16B は不成立) | 採る | §3-1 の実サイズ、固定表は BSS、`device_boot_map` は動かさない、`STATIC_ASSERT` |
| P3 | 失敗時全不変の「統計」の定義、DMA の `*out=0` を直接渡さない | 採る | §3-2 (会計 / 診断)、§3-7 |
| P3 | owner 番号の返却・再利用、固定 SURFACE、bundle 移譲後の回収で L2 / L3 のどちらを更新するか | 採る | §3-1 (寿命の規則、更新の表) |
| P3 | 背景の RESERVED / MMIO と装置予約の区別を拒否規則に | 採る | §3-1 (BACKGROUND 型と拒否規則)、§2-2 の 11. (今の核は 17MB 以上で PEGC の予約を拒む) |
| P3 | `objdump` 一致は補助証拠。静的初期化子と表歩きの移動は別に検証、USER・RW の判定の保持 | 採る | §3-4、T1f の受入 |
| P3 | kselftest ⑤ は ⑥ より前 — gfx 登録後と GUI 移譲後の検査地点 | 採る | §3-2 `ledger_selfcheck(tag)`、§3-3 ⑤ / ⑥ / ⑨ |
| P3 | auto で planar と PEGC の backing が共存 — 「BB の SURFACE が 1 本」の意味 | 採る | §3-8 (SURFACE の本数) |
| P3 | shlib の `pgalloc_free_n` は 11 か所、P2V 表は 161 件 (全件監査済みとは書かない) | 採る | §2-1、§2-3 |
| P3 | Cirrus の「部分適用済み」コメントが古い側 (`paging_map_phys` は PT 準備後に PTE 更新) → T1-U5 解消。V86 の teardown は longjmp 後 | 採る | §2-2 の 7.、§2-1 V86 の行、§5-2 |
| P3 | 32MB / 128MB は 64MB の代替証拠ではない。実機の観測手段は `/api/mem` 以外 | 採る | §4-0 |
| P3 | 12MB 前後の移行境界はホスト試験で | 採る | §3-3、§4-1 (3,073 / 3,074 PFN) |

**同種の穴を自分で探して直したもの** (Codex の指摘の延長):

- `irq_in_irq` の利用者は `irq_register` / `irq_unregister` だけ (`irq.c:56,82`) で、他に判定に使う所は無い — broker を触らない限り B1 の反例は閉じる。
- longjmp 点は `exec_exit` の 1 つではなく 6 つ (`exec.c:1389,2306,2357,2416,2474`、`v86.c:506`) — 個別に戻すのではなく jmpbuf に控えて `exec_longjmp` で戻す (§3-5)。
- 例外の入口は `isr_common` だけでなく `isr_stub_13` / `isr_stub_14` の V86 分岐と `ISR_*_V86` マクロ・`isr_stub_default` にもあり、V86 の #GP 反射は `iretd` で復帰する — 復帰点の −1 を全部に (§3-5)。
- 静的初期化子は Codex の 4 例に加えて `gfx_core.c:18-20` の 3 本と `kcg.c:53-55` の 3 本 (合計 10)、マクロ定義 3 か所 (`isr_handlers.c:58-59`、`kcg.c:215`、`kapi_db.c:83` — 最後は仮想) (§2-2 の 10.)。
- 起動順の依存は PCI だけでなく **`kselftest_run` の `test_dma_pool` が末尾で `dma_pool_init()` を呼び直す** (`kselftest.c:915`) — 台帳の DMA 区間は静的なので影響しないことを ④ に明記。`GFX=` の読み取りの依存は `vfs_read` だけ (T1-R4 を静的に解消)。
- 12KB の予約域は DMA プールの**上側ガード** (02_memory.md §2-1) — 全部を backing にすると池のはみ出しを止めるページが消える。1 ページを残して 8KB にした (§3-3)。
- Xe10 のリニア窓は decode 4MB / 写像 2MB (`wab_xe10.h:173-178`) — 票の「4MiB の写像」を訂正し、X4 の実例にした (§3-8)。
- 今の broker は `device_boot_map` の RESERVED / MMIO で拒否する (`pgalloc.c:437`) ので、T1a で 15〜16MB を RESERVED に登録したままでは 17MB 以上で PEGC の予約が通らない (§2-2 の 11.)。
- `sys_usable_mem_end` の利用者は `exec_child_claim` と `ring3_band_ram_top` (`exec.c:942,971`) — B2 の exec 上端はアプリ帯の上限にも効く。
- ~~CPL=0 子の claim を owner 付きにすると、今は黙って共有していた「GUI の下からの `--cpl0`」が明示の拒否になる~~ — **2 回目で訂正**: GUI からの `--cpl0` は入口の `appslot_cpl0_admit` が既に `OS32_ERR_INVAL` で断っている。claim の owner 検査は第二の防御線で、挙動の変化ではない (§3-6、T1-R6。T1-U8 は撤回)。

### 6-3. Codex レビュー 2 回目 (gpt-6-astra、2026-09-30、`b9a2df3`) の所見と対応

判定は **Request changes**。1 回目の B1・B2・B4・B6 は設計上閉じた、B3・B5・B7 は元の反例は閉じたが改訂で新しい問題 (B10・B11・B8) が出た、という判定。D1〜D36 の変更は求められていない。

| # | 所見 (要旨) | 採否 | 対応 (この票の場所) |
|---|---|---|---|
| **B8** (P1) | `v86_jmpbuf[6]` (`v86.c:59`) が長さを直書きしていて `KSETJMP_BUF_LEN + 2` に追従しない — `v86 -t` で保存先を越えて書く | **採る** | §2-2 の 13.、§3-5: `KSETJMP_BUF_LEN` を 8 にし、保存先 3 か所 (`appslot.h:102`、`exec.c:1316`、`v86.c:59`) を全部その定数に。`g_exit_jmpbuf` の写しは定数の語数。T1b の変更一覧に `v86.c:59`。`grep` の検査 (T1-R8) |
| **B9** (P1) | 「Cirrus の SURFACE は T1 では 0 本」のまま `gfx_bb_phys_range` を SURFACE 参照にすると、`cirrus-on` の GUI アプリがクライアント面 (0xFE04B000、300KB) の USER 昇格を失って #PF | **採る** | §2-1 (Cirrus の面の行)、§2-2 の 14.、§3-1 (`backend` / `role`)、§3-8: CLIENT と DISPLAY を MMIO backing の SURFACE として ⑥ で登録、`gfx_bb_phys_range` は選択中 backend の CLIENT を返す、`fb->planes[0]` も同じ情報源。T1e の回帰に `cirrus-on` の GUI 描画アプリ (#PF 0 件) |
| **B10** (P2) | `ledger_resource` は decode 1 つ、`ledger_reserve_set` も `res` 1 つ — auto の 3 窓 (PEGC / Xe10 銀行 / Xe10 リニア) を 1 レコードで許可できず、正当な起動が拒否されるか照合が抜ける | **採る** | §3-1 (`res_mask`、拒否規則 (e))、§3-2 (`ledger_span = {first, end, kind, res}`)、§3-8 (span 3 本、各自の資源、併合後の `res_mask`)。T1d のホスト試験に span ごとの照合と Trident 型 / 82557 型 |
| **B11** (P2) | T1b 単独では `pegc_reserve_backbuffer` が `sys_reserve_top` → `pgalloc_reserve_pfn` (kernel owner の永久予約) の直後に boot で claim するので必ず拒否 | **採る** | §2-2 の 17.、§3-1 (永久予約の owner は呼び手が渡す)、§3-2 (暫定 `sys_reserve_top(owner, bytes)`)、§4-2 (T1b で boot を渡し `mark_used` を消す)、**§4-8 の表** (段の境目ごとに全部の口の owner) |
| P3 | T1-R6 の「新しい挙動変化」は誤り (`appslot_cpl0_admit` が GUI 起動を `OS32_ERR_INVAL` で拒否、試験 20A〜20E)。「gshell の PD/PT が 0x500000 付近」も誤り | 採る | §2-1 (CPL=0 の行)、§3-6、§5-1 T1-R6 を書き直し、T1-U8 は撤回 |
| P3 | `v86 -t` は CPL=3 (`app.conf:85` `cui`) なので CPL=0 claim の受入にならない | 採る | §4-1 / §4-2: `mkos32x --cpl0` の試験バイナリ `userland/tests/cpl0_probe` を足して受入に |
| P3 | IRQ7 の専用スタブは `RESTORE_KSEG` をしない — V86 からの入口でも安全に深さを更新する方式 | 採る | §2-2 の 15.、§3-5: `ss:` セグメントで更新 (全スタブ共通) |
| P3 | 資源レコードは自然配置で 36B | 採る | §3-1: 欄を u32 → u16 → u8 の順に並べて 32B、`STATIC_ASSERT` |
| P3 | Xe10 の decode 4MB / 写像 2MB は NP21/W の窓の値で実機の実測ではない | 採る | §2-1、§3-1、§3-8、T1-R9 / T1-U9 |

**同種の穴を自分で探したもの (2 回目)** — 見た範囲と結果:

- **固定長の保存先**: `grep -rn 'KSETJMP_BUF_LEN\|jmpbuf\[' kernel exec include gfx drivers fs kapi lib sdk userland tools/tests` — 保存先は 3 か所 (§2-1 の行) で、直書きは `v86.c:59` の 1 つだけ。`setjmp.asm:8-10` の配置コメントも直す。SDK / userland / ホスト試験に写しは無い (`tools/tests/k5b_kernel_tdd.md` は記録文書)。**見た**。
- **SURFACE に切り替える他の利用者**: `bb_base` / `bb_size` の読み手は `gfx_bb_phys_range` (`gfx_core.c:147-149`、呼び手 `exec.c:1959` の 1 か所) と `fb->planes[0]` (`gfx_core.c:124`) の 2 つ。書き手は 3 backend (`backend_pc98.c:154` 静的初期化子、`backend_pegc.c:705`、`backend_cirrus.c:380`)。→ §3-8 で両方の読み手を SURFACE から引く。**見た**。libos32gfx (userland 側) が `bb_base` を KAPI で受ける経路は `gfx_screen_info` (`gfx_core.c:156`) 経由で、そこは backend の `query` が返す — 番地の情報源は同じ `bb_base` なので SURFACE 化の後も一致する。**見た** (userland 側の使い方の全部は**見ていない**)。
- **複数資源の予約を要する他の要求**: Ra266 の列挙 (§2-2 の 16.) — Trident (memory BAR 3 + ROM)、82557 (memory BAR 2)、NEC display (BAR 0)、CA0106 (I/O)。PCM CS4231 は I/O だけ。→ `ledger_span` の `res` は span ごと、T1d の試験に Trident 型 / 82557 型。**見た** (実機の BAR の幅は**見ていない** — T4 以降の sizing)。
- **段の境目で owner が食い違う組み合わせ**: 今の予約・確保の口 12 種の呼び手を全部並べ (§4-8)、T1a〜T1f の各段の終わりに渡る owner を書いた。空欄 (owner 無し) が残るのは T1a の終わりだけ (T1a は L2 が無いので問題ない)。**見た**。`dma_pool_alloc` は池の外なので owner の対象外。
- **深さの更新の安全性**: 全スタブの `RESTORE_KSEG` の有無 (`isr_stub.asm:35-41` の定義と 14 か所の使用) を並べ、IRQ7 だけが無い。`ss:` で書けば全部同じ形になる。**見た**。
- **入口判定と claim の重なり**: `appslot_cpl0_admit` / `appslot_cui_only_admit` / `appslot_start_admit` (`appslot.c:178-200`) と `exec.c:1775` の順を確認 — claim は入口判定の後。**見た**。

### 6-4. Codex レビュー 3 回目 (gpt-6-astra、2026-09-30、`23ebc63`) の所見と対応

判定は **Request changes** (B10 の残存と新規 B12 の 2 件)。B8・B9 は設計上閉じた、B11 は元の反例が閉じた、B1・B2・B4・B6 の再破壊なし、という判定。D1〜D36 の変更は求められていない。**往復の上限に達したため、ユーザー決定 (2026-09-30) で 2 件と P3 を Codex が示した形で直し、差分だけを Codex に確認させて確定する。**

| # | 所見 (要旨) | 採否 | 対応 (この票の場所) |
|---|---|---|---|
| **B10 継続** (P2) | 資源レコードの decode 区間は 1 組なので、Xe10 の銀行窓 `[0xF60000, 0xF68000)` とリニア窓 `[0xFE000000, 0xFE400000)` を同じレコードで表すと、`GFX=cirrus` / `auto` の通常起動で片方が包含検査に落ちる (包む巨大区間にすると未確認の隙間を検証済みにして X4 違反) | **採る** (Codex の示した形) | §2-2 の 16. (見積り 9 本)、§3-1 (1 レコード = decode 区間 1 組、aperture ごとに分割)、§3-8 (PEGC / Xe10-bank / Xe10-linear の 3 レコードと各自を指す 3 span、低位区間の `res_mask` = {PEGC, Xe10-bank}、高位 = {Xe10-linear})、T1d / T1e の試験、T1-R7 |
| **B12** (P2) | §4-8 で `paging_app_band_selftest` の 2 ページ (`paging.c:1144`) を owner = kernel としたが、`:1165` の `paging_addrspace_free_user_range` は AS の owner で解放する — 解放が拒否され空きページ数の比較が落ちる | **採る** (Codex の示した形) | §4-8 の行 (試験用 AS の owner を明示的に取り、PD / PT と 2 ページをその owner で確保・解放、解放検査は緩めない)、§3-2 の呼び手の書き換え |
| P3 | `ss:` の説明: V86 / CPL=3 からは TSS.ss0 = 0x10、CPL=0 からは既存の SS (GDT ロード時の flat SS) で同じ番地 | 採る | §2-2 の 15.、§3-5 |
| P3 | MMIO SURFACE の移譲 (Cirrus CLIENT boot → gshell は SURFACE の owner だけ、DEVICE 区間の `gfx` と DISPLAY の kernel は保つ) を表に | 採る | §3-1 の移譲・回収の表 |
| P3 | §4-8 の「全部が owner 付き」と `—` の意味 (DMA は対象外、workspace は固定 kernel owner、T1f の `—` は前段継承) | 採る | §4-8 の冒頭 |

**コードで確かめたもの (3 回目)**: `paging_app_band_selftest` (`paging.c:1063`) は試験用 AS を 2 回作る (`:1082`、`:1127`)。2 ページは `pgalloc_alloc_n(2)` (`:1144`) で確保し、`paging_addrspace_map_user_range_phys` で試験用 AS に写し、`paging_addrspace_free_user_range` (`:1165`、本体 `:908-932` は PTE を辿って `pgalloc_free_page` を呼ぶ) で返し、`pgalloc_free_pages() != base_free` (`:1169`) で比較する — B12 の経路どおり。`kernel_tss.ss0 = KERNEL_DS` (`tss.c:36`、`KERNEL_DS` = 0x10 は `:27`)、`x86_load_gdt` (`x86_desc.h:24`) は SS を 0x10 に再ロードする (`:34`)。

---

## 7. ユーザー判断が要る点

**決定 (D 番号) を変えるものは無い。** Codex 1 回目〜3 回目の対応もすべて票の内側 (段の順の入れ替えは T1 の中の分割で、§6 の T1 → T2 → T3 の順序は変わらない。`cpl0_probe` の追加は受入のための試験バイナリで、`--cpl0` の撤去 (T2) の決定は変えない)。実装の途中で次が起きたときだけユーザーに上げる:

1. **X15** — カーネル本体の予算 13KB に T1 が入らず、T3 の一部 (固定 PT を画像の外へ) を T1 の前へ動かす必要が出たとき (順序の例外)。
2. **64MB の NP21/W 構成** — Ra266 の実機で見るのが基本。実機が使えない期間に 64MB ちょうどが要るときだけ、preset `ram-64mb` の追加 (ツールの拡張 + [D2])。

NP21/W の `ExMemory` の切り替え ([D2]) と Ra266 での確認 (実機の物理操作) は、各段の受入の時点で個別承認。

**Codex 3 回目で見てもらった点** (2 回目の反映の確認。結果は §6-4): (1) §3-5 の jmpbuf の 3 保存先の統一 (B8) と `ss:` の深さ更新 (IRQ7 を含む) で漏れが無いか。(2) §3-8 の Cirrus CLIENT / DISPLAY の SURFACE と `gfx_bb_phys_range` / `fb->planes[0]` の情報源の一本化 (B9) — probe 失敗で PC98 に落ちたときの選択の扱い。(3) §3-1 / §3-2 の span ごとの資源照合と `res_mask` (B10) が X4 の「予約権限は検証済みレコード」を保つか。(4) §4-8 の段の境目の owner 表 (B11) に空欄・食い違いが無いか。(5) §3-6 の入口判定不変 + owner 付き claim + `cpl0_probe` の受入 (P3 の訂正)。
