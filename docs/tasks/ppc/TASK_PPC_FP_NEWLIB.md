# TASK_PPC_FP_NEWLIB — PowerPC 移植: ハードウェアの FPU とユーザーランドの newlib

> 上位: [TASK_PPC_SHELL](TASK_PPC_SHELL.md)

> 状態: **実装中 (2026-11-18 再開)** — 11-17 にユーザー指示「分離統合を先に」で止め、分離の完了 (TASK_PPC_SEP) を受けて再開した。波 A・B と N1・N2 は main、N3 と KAPI の直しは wt/ppc-newlib (§3、11-18 に main を取り込み、QEMU の受入を回し直す)。設計は確定 ([PPC_FP_NEWLIB_DESIGN](../../decisions/ppc/PPC_FP_NEWLIB_DESIGN.md))。

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

## 3. 今の状態 (2026-11-17、止めた時点)
| 段 | 状態 |
|---|---|
| 波 A・B (FPU の保存・復元・初期化、USER の MSR[FP]、0x800、旗と監査) | main (int-1117b、10680c11)。QEMU の受入 tools/accept/ppc_fp.py は 7450・7455 で正像 GREEN、変異 5 本が理由どおり RED |
| N1・N2 (nano の道具の PPC の分岐・帳簿の PPC の節・cross-ppc の記録・app.ld の小さいデータの節) | main (int-1117c、028023fa) |
| N3 (USER を newlib に結線、crt0 の _init と help) と PPC の KAPI の直し (arch/ppc/kapi_call.c の caller と va_next) | 枝 wt/ppc-newlib。本線の統合の枝 int-1117e で CI の追従をしていた。tools/accept/ppc_newlib.py は 7450・7455 で正像 GREEN、変異 3 本 RED (sbss はゲストで見えないので host の試験に任せた) |
| N3 の CI の追従 (tools/accept/arch_ctx.py のリンクの閉包、halt-ee の目印) | wt/ppc-newlib a65d4e53。リンクは通ったが、QEMU の --mutate の受入は途中で止めた (未検証)。統合に入れる前に受入を回すこと |
| S (カーネルの SQLite の別スタックの .S と口) | 止めている |
- 受入で見つけて直した物: PPC の KAPI の kprintf が caller 未初期化と va_next 未実装で kill されていた (arch/ppc/kapi_call.c)。RAM root の ISO は level 1 なので USER の bin の名前は 8.3。
