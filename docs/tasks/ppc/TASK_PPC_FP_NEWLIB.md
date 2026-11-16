# TASK_PPC_FP_NEWLIB — PowerPC 移植: ハードウェアの FPU とユーザーランドの newlib

> 上位: [TASK_PPC_SHELL](TASK_PPC_SHELL.md)

> 状態: **実装中 (2026-11-16)** — 設計は確定 ([PPC_FP_NEWLIB_DESIGN](../../decisions/ppc/PPC_FP_NEWLIB_DESIGN.md))。段は波 A (宣言) から。

## 0. ユーザー決定 (2026-11-16)
- 「sqlite ですが、newlibc が入ってないのでは？ newlibc に追加した x86 依存のアセンブラも移植して」。newlib は無改変で、x86 依存の asm に当たるのはカーネルの kstring_asm と sqlite_stack (本線の確認)。
- 他アーキのユーザーランドも newlib (POLICY_DEV §1、AC-LIBC-1)。同じ版・configure・無改変・同じ adapter の方式。
- 「カーネルに libgcc が入るのはまずいです。x86 に習ってください」。
- 「ppc は基本的に g3 以上が対象なので fpu があると思います」。PPC は判定せずハードウェアの FPU を使う。
- x86 は「fpu があるかを判定してからハードウェアの fpu を使う様に改修」(本線の段)。

## 1. 段
- 確定設計 §7 の表の波 A (宣言) → B (FP 実体・guard・結線・旗・監査) → N1 (nano の道具と帳簿) → N2 (CRT と配置) → N3 (USER の結線) → S (カーネルの SQLite の別 stack の口) → T (試験と受入)。
- 受入は QEMU 7450・7455。750 (G3) は USER と SQLite の TU の最終 ELF の静的監査だけ。G3 の起動対応は範囲外 (別の段、ユーザーに後で諮る)。
- 共通の場所 (include/exec_arch.h・sdk/allocator/nano_inputs.json・tools/ci/build_cross_ppc.sh・kernel/sqlite_engine.c・build/features/sqlite.mk・lib/sqlite3/・tools/checks/) に触る段は、統合の枝に入れる前に本線に知らせる。x86 の生成物と nano の bytes は変えない。
