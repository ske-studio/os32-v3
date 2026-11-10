# TASK_PPC_KBOOT — PowerPC 移植: OS32 本体を QEMU mac99 で初期化まで通す (到達点 ②)

> 上位: [TASK_PPC_SEP](TASK_PPC_SEP.md)。後続: [TASK_PPC_LAN](TASK_PPC_LAN.md)、[TASK_PPC_SPLASH](TASK_PPC_SPLASH.md)

> 状態: **受入完了・実機確認待ち (2026-11-10)** — QEMU mac99 (7450・7455) で ② を通過。統合の枝 wt/ppc-kbh eaca0e75、証拠 ~/os32-tmp/evidence/2026-11-10/ppc-kboot/RESULT.md。実機は DEFERRED_TESTS の PPC-KBOOT-RH。実装と取り込みの決まりは [PPC_IMPL_RULES](../../decisions/ppc/PPC_IMPL_RULES.md)。

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
| T3 | 受入の台本 kboot_run.py と自己試験 | f9513c5f | 仮の像で自己試験 (14 RED・4 GREEN・台本の変異 5) | 可 (3 回) |
| H1 | 閉包の未解決の provider arch/ppc/kboot_unavailable.c (ring3_call_from_user の 1 件) | (kbh) | 本体の link | 可 (H2・H3 と一緒に 3 回) |
| H2 | 本体の初期化 init/ppc_kernel.c、probe arch/ppc/kernel_probe.S | (kbh) | QEMU 7450/7455 | 同上 |
| H3 | 本体の link build/ppc-kboot.mk、監査 tools/ppc/kboot_audit.py、受入 tools/ppc/kboot_accept.py | (kbh) | 負像 2・製品の変異 10・監査の変異 4 | 同上 |
| T4 | 回帰の入口 tools/ppc/kboot_all.py | (kbh) | 20/20 | PM が読んで着地 |

H1〜H3・T4 は統合の枝 wt/ppc-kbh (全段を 1 本にまとめた枝) にある。

## 3. 受入 (2026-11-10)

合 — 証拠 ~/os32-tmp/evidence/2026-11-10/ppc-kboot/RESULT.md。入口は `make -f build/ppc-kboot.mk` と `python3 -B tools/ppc/kboot_accept.py --mutate`、回帰は `python3 -B tools/ppc/kboot_all.py --mutate`。x86 の生成物は main 2259debc と全一致 (build_compare)。

未確認・持越し: G4 実機 (PPC-KBOOT-RH)、E3 (本線の PCI IRQ の共通部の後)、16MiB を超える RAM と system の明示の予約、tick の単位 (PIT_HZ=100 の利用者は ② の閉包に無いことを監査で保証、③ の前に整合)、SHM の共有 USER の写像の trampoline (② は実行しない RO の 0 ページ、exec の段で差し替え)。次は決定 (c) の LAN (GMAC と Host Services)。
