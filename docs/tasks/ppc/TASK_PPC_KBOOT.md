# TASK_PPC_KBOOT — PowerPC 移植: OS32 本体を QEMU mac99 で初期化まで通す (到達点 ②)

> 上位: [TASK_PPC_SEP](TASK_PPC_SEP.md)。後続: [TASK_PPC_SPLASH](TASK_PPC_SPLASH.md)

> 状態: **実装中 (2026-11-10)** — ユーザーの指示で文書の整理のためいったん停止中。第 1〜3 波と D3・T3 (先行) まで実装 (下の §2)。次は H1 → H2 → H3 → 受入。実装と取り込みの決まりは [PPC_IMPL_RULES](../../decisions/ppc/PPC_IMPL_RULES.md)。

## 0. ユーザー決定

- 2026-11-09 (os32-v3 の PM 経由のユーザー選択「QEMU で初期化が通る」): 本線の実装の再開は、QEMU mac99 で ② (OF から入り MMU・例外・MPIC・tick が動いてカーネルの初期化をユーザーランドの手前まで通し、起動のログが出る) を通した時点。G4 実機の確認は後追い。シェル (③) は条件に入れない。
- 2026-11-09 決定 (c) (票 TASK_PPC_SEP): ② の直後は内蔵 Ethernet (GMAC) と Host Services の LAN 経由のログ・遠隔操作を先に置く。
- 「ユーザーランドの手前」は core-ready 境界 (実ページ管理・heap・shm が online、時刻と割込みが継続、起動ログ。VFS・root mount・exec_init・module 読込みは含めない) と解釈して進める (PM、ユーザーへ報告済み)。

## 1. 詳細設計 (確定)

全文は [PPC_KBOOT_DESIGN](../../decisions/ppc/PPC_KBOOT_DESIGN.md)。要点:

- kboot profile = `init/ppc_kernel.c` から到達する symbol の閉包 + 明示した追加 TU。PC98 の `init/kernel.c` は呼ばない。console の TU は選ばず、G2 が console_write/shell_print を直に出す暫定経路 (分離の未着段を待たない)。
- mm は案 D。管理 RAM は 16MiB に限定し geometry {15M,15M,16M,16M,16M}。HPT は 128KiB (allocator の外)。ESCC・MPIC の alias は device aperture の静的 PT の固定 offset。
- trap は DR=0 で frame を作り、spill は real のまま。0x500 と fatal の MMIO は frame 完成後に DR=1 の I/G alias で触る。
- MPIC は CPU0・TPR 0、配送試験は自己 IPI。decrementer は 1000Hz の周期加算型 (周期は platform が与える)。
- 受入は新しい `tools/ppc/kboot_run.py`: 60 秒以内に `KBOOT` の 9 行が順に出て、READY の後に tick が TB と ±5% で継続 (7450・7455 の 2 回) と負試験。

| 波 | 段 |
|---|---|
| 1 | A1 link 配置・B1 OF snapshot・C1 PD walk・G1 log |
| 2 | A2 entry・B2 OF 配置・C2 HPT・D1 frame・E1 MPIC 発見・F1 時計 |
| 3 | B3 mm geometry・C3 root/flush・C4 CPU・D2 trap・E2 MPIC 駆動・F2 DEC・G2 console |
| 4 | D3 boot 口・H1 非対応 provider (E3 IRQ adapter は ② の後、PM 決定) |
| 5 | H2 init → H3 本体 graph → T1〜T4 受入 |

## 2. 段の状態 (2026-11-10)

各段は PowerPC の枝 `wt/ppc-kb<段>` (push 済み、`[skip ci]`)。x86 の生成物は変えない (PPC だけ)。試験は tools/ppc/kboot_*.py (make check の外)、QEMU の試験は QEMU 8.2.2 の mac99。独立レビューは gpt-6-astra。main への統合は ② の受入 (T3) の後にまとめて行う。

| 段 | 中身 | 枝の先端 | 試験 | レビュー |
|---|---|---|---|---|
| A1 | arch/ppc/kernel.ld・platform/powermac/kboot_layout.h | aed5df2f | link と ELF | 可 |
| A2 | 入口 kernel_entry.S | 6968df1a | QEMU で OF から起動 | 可 (2 回) |
| B1 | OF の snapshot kboot_capture.c | 5af1aa7e | host | 可 (2 回) |
| B2 | 配置の検証と HPT の切り出し kboot_reserve.c | 3b2a5953 | host | 可 (2 回) |
| B3 | mm の geometry と memory の provider | 4fd01821 | host (共通 mm の製品を組む) | 可 (PM 決定 2 件) |
| C1 | PD/PT の walk pd_walk.c | 77a5a787 | host 16384 通り | 可 |
| C2 | HPT の spill と全消去 kernel_hpt.c | d66b9fa7 | host | 可 |
| C3 | root と flush kernel_mmu.S | e9665883 | QEMU | 可 (2 回) |
| C4 | CPU・BAT の撤去・SR・SDR1 kernel_cpu.c | 433fbf3b | QEMU 7450/7455 | 可 (2 回) |
| D1 | 例外の vector kernel_vectors.S | 522278f0 | QEMU | 可 (2 回) |
| D2 | trap の分類と fatal kernel_trap.c | dbc5e8d0 | host と QEMU | 可 (2 回) |
| D3 | boot の口 kernel_boot.c・kernel_takeover.S | a57b3fe2 | QEMU 7450/7455 (通常・低位 guard・HPT fatal) | 可 (MPIC の窓の解決は暫定、H2 で E1 に置換) |
| E1 | MPIC の窓の解決 mpic_tree.c | d0ca1252 | host | 可 (2 回) |
| E2 | MPIC の駆動 mpic.c | f6dd81ad | QEMU で自己 IPI 2 回 | 可 |
| F1 | 時計の provider clock.c | 32fe83ad | host | 可 (2 回) |
| F2 | decrementer kernel_dec.c | e4a3dfb2 | QEMU で TB と −0.05% | 可 (2 回) |
| G1 | log.c (ESCC と後段の sink) | e9cee64b | host | 可 |
| G2 | 暫定の console kboot_console.c | b32818ee | host (lib/kprintf.c を組む) | 可 (2 回) |
| T3 | 受入の台本 kboot_run.py (先行) | a8192320 (直し 1 回目まで) | 仮の像で自己試験 (3 回で判定一致) | 第 2 回で P2 が 2 件残り (停止の検出の変異を復元、最後の継続を tick 増分と経過時間の ±5% で照合して低速 tick の像を足す)。再開の最初に直す |

残り: H1 (閉包の未解決の分類と kboot 専用の provider) → H2 (init/ppc_kernel.c、各段の結線: D3 の仮の MPIC の解決を E1 に置き換える) → H3 (kboot の profile と本体の link、PIT_HZ の利用者の監査) → T1・T2・T4 → T3 で 7450/7455 の受入。E3 は本線の PCI IRQ の bitmap の後。
