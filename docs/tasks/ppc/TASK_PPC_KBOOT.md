# TASK_PPC_KBOOT — PowerPC 移植: OS32 本体を QEMU mac99 で初期化まで通す (到達点 ②)

> 上位: [TASK_PPC_SEP](TASK_PPC_SEP.md)

> 状態: **実装中 (2026-11-10)** — 設計確定、第 1 波 {A1,B1,C1,G1} から実装。

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
| 4 | D3 boot 口・E3 IRQ adapter・H1 非対応 provider |
| 5 | H2 init → H3 本体 graph → T1〜T4 受入 |
