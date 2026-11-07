# TASK_PPC_MMU — PowerPC 移植: OF からの切り離しと例外・変換

> 上位: [TASK_PPC_SEP](TASK_PPC_SEP.md)

> 状態: **受入完了・実機確認待ち (2026-11-07)** — QEMU の受入を通過し main に入った。証拠 ~/os32-tmp/evidence/2026-11-07/ppc-mmu/RESULT.md。確定設計は [PPC_MMU_DESIGN](../../decisions/ppc/PPC_MMU_DESIGN.md)。実機は DEFERRED_TESTS の PPC-MMU-RH。

## 0. ユーザー決定

- 2026-11-07 「案Dで」: カーネル本体を繋ぐときは案 D。この段は mm をリンクしない案 C で、書いた物を D で使い回す (計画は票 TASK_PPC_SEP)。

## 1. 順の変更

PPC_BOOT_DESIGN の見取り図は FB の直描きを MMU より先に置いていたが、QEMU の到達点を先にした。切り離しの後は実機に観測路が無いので、切り離しの直前に OF へ `TAKEOVER` を出す。実機に進む前に FB を入れる。

## 2. 実行

`make -f build/ppc.mk`、`python3 tools/ppc/mmu_run.py --timeout 30 [--mutate] [--stage A|B|C]`。計測は `python3 tools/ppc/check_compile.py`。
