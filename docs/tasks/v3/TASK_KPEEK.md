# TASK_KPEEK — 実機でシリアル経由にメモリを読む (kdebug の peek)

> 状態: **詳細設計中 (2026-10-20)** — **T7 の後の最初の実装** (ユーザー決定 2026-10-20)。詳細設計は T7 の実装と並行して先に起こす。

## 0. ユーザー決定と背景 (2026-10-20)

- 実機には NP21/W の `emu_read_mem` に当たるものが無い (rshell over serial はファイルの hexdump まで)。Valgrind の移植・sanitizer の拡大・NP21/W の memcheck は、OS32 の不具合の型 (約束の取り違え・実機差・順序) に対して効果が薄いので見送った。代わりに **実機でシリアル経由に特定のメモリをダンプする** 道具を作る。
- **T7 の後の最初の実装**にする。詳細設計は先に起こしてよい。
- 置き場は kdebug の部品 (`/boot/kdebug.bin`、`KDEBUG=1` で起動したときだけ読み込む — [TASK_DBGREF](TASK_DBGREF.md) の方針)。製品の像には載せない。
- 既定は RAM の読み出しだけ。MMIO は明示の指定と許可リストを要る (読むだけで状態が変わるレジスタがある — [TASK_GEFORCE6200](../realhw/TASK_GEFORCE6200.md) §3 の段 1)。読み出し専用で、書き込みは持たない。
- ホスト側は `tools/rshell_serial.py` に `peek <記号名|番地> <長さ>`。記号名は動いているカーネルと同じ版の ELF (`ver` の Commit と Image CRC で照合) から番地に直す。
- GeForce 6200 の観測の段 1・2 (BAR0 の読み取り、VBIOS の吸い出し) はこの道具で行う (MMIO の許可リストを足して回す)。

## 1. 詳細設計

(Fable が起草 → Opus レビュー。`~/os32-tmp/run/kpeek/DESIGN.r1.md` を確定させてここへ要約する)
