# TASK_KPEEK — 実機でシリアル経由にメモリを読む (kdebug の peek)

> 状態: **設計中 (2026-10-20、詳細設計は確定・実装は T7 の後)** — **T7 の後の最初の実装** (ユーザー決定 2026-10-20)。詳細設計は T7 の実装と並行して先に起こす。

## 0. ユーザー決定と背景 (2026-10-20)

- 実機には NP21/W の `emu_read_mem` に当たるものが無い (rshell over serial はファイルの hexdump まで)。Valgrind の移植・sanitizer の拡大・NP21/W の memcheck は、OS32 の不具合の型 (約束の取り違え・実機差・順序) に対して効果が薄いので見送った。代わりに **実機でシリアル経由に特定のメモリをダンプする** 道具を作る。
- **T7 の後の最初の実装**にする。詳細設計は先に起こしてよい。
- 置き場は kdebug の部品 (`/boot/kdebug.bin`、`KDEBUG=1` で起動したときだけ読み込む — [TASK_DBGREF](TASK_DBGREF.md) の方針)。製品の像には載せない。
- 既定は RAM の読み出しだけ。MMIO は明示の指定と許可リストを要る (読むだけで状態が変わるレジスタがある — [TASK_GEFORCE6200](../realhw/TASK_GEFORCE6200.md) §3 の段 1)。読み出し専用で、書き込みは持たない。
- ホスト側は `tools/rshell_serial.py` に `peek <記号名|番地> <長さ>`。記号名は動いているカーネルと同じ版の ELF (`ver` の Commit と Image CRC で照合) から番地に直す。
- **出口はシリアルと LAN の両方を持つ** (ユーザー決定 2026-10-20)。シリアルは既定で常に使える経路 (LAN や PCI そのものを調べるときも動く)。LAN は既存の lan.mod の送信で、ゲスト→ホストへ一方向にダンプのフレーム (通し番号・長さ・CRC) を流し、ホストの受け口がファイルに組み直す。コマンドと最後の ok/失敗の行はシリアルの rshell。LAN が使えないとき (lan.mod が動いていない・ペア未設定) はシリアルに戻ってそう 1 行出す。LAN の出口はシリアル版の後の段。
- GeForce 6200 の観測の段 1・2 (BAR0 の読み取り、VBIOS の吸い出し) はこの道具で行う (MMIO の許可リストを足して回す)。

- 外部デバイスの実装方法のモデル (専用の文書群、[ROADMAP](../../ROADMAP.md) の長期テーマ) で、実機観測の道具として扱う。

## 1. 詳細設計 (確定 2026-10-20、Fable 起草・Opus 3 往復・PM が最終修正、ユーザー判断は無し)

全文 (実物の確認 10 件、書式、試験と変異、変更一覧): `~/os32-tmp/run/kpeek/DESIGN.final.md`。

- **入口**: shell の `peek` コマンド → KAPI `kdebug_call` を 1 本追記 ([ABI2]、製品像に増えるのは受け口だけ) → kdebug 部品の新しい段 (S5、ops の slot 19)。
- **番地空間**: `-k` カーネル VA (読む前にページごとに写像を確かめ、PCD/PWT のページと [A0000h, E8000h) は DENY)、`-p` 物理、`-m` MMIO (PCI の BAR を確かめ — I/O BAR・未割当・Memory 無効は DENY — 許可リストの範囲だけ、幅 1/2/4 を指定、デバイス窓の先頭 4MB の PT を irq_save の中で読み専用に付け替え、触る 2 PTE だけ退避して戻す、4MB の境界をまたぐ要求は ARG)。書き込みは持たない。
- **出力**: 行ごとに番地・16 進・8 bit 和、最後に全体の CRC32 と `ok len= frames=` の行。ホストは連番・和・CRC・len を全部照合する。
- **ホスト**: `tools/rshell_peek.py` と `rshell_serial.py peek <記号名|番地> <長さ>`。記号名は ELF の `os32_build_commit` (nm の番地から NUL まで) と Image CRC の両方が動いているカーネルと一致したときだけ引く。
- **LAN の出口 (T5)**: SerialFS に片道の `SFS_T_DUMP` (通し番号付き) を足し、lan.mod の 82557 の送信でホストのペア MAC へ流す。`lanfs_host.py` が受けて組み直す。LAN が使えなければシリアルへ戻って 1 行出す。NP21/W (LGY-98) では常にシリアル。
- **段**: T1 (KAPI と ops の段、`make clean`) → T2 (kdebug の kpeek と許可リスト) ∥ T3 (shell とホストの道具) → T5 (LAN) → T4 (受入: NP21/W で `emu_read_mem` と一致、実機)。T1 の最初に NP21/W の `lspci -v` で PCI の Cirrus (1013h) の有無を見て、無ければ窓の試験は実機 (GeForce の段 1) へ持ち越す。
- 依頼パックに書くこと: 末尾「確定」の L1〜L5 と、`grep -rln 'S4\|MODULE_OPS_SLOTS\|KDEBUG_OPS_NAMES' tools/` の追従。
