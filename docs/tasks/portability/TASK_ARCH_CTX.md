# TASK_ARCH_CTX — 実行文脈ごとの約束 (ARCH_COMPAT W4、AC-CTX) を全 arch にそろえる

> 状態: **設計中 (2026-11-14)** — 設計は確定して実装を待つ。詳細設計は [ARCH_CTX_DESIGN](../../decisions/ARCH_CTX_DESIGN.md)。
> 関係: 互換契約 ARCH_COMPAT_CONTRACT の W4。PowerPC の P3 (LAN の SH) はこの票の受入まで止める。

## 0. ユーザー決定 (2026-11-14)
| 項目 | 決定 (原文) |
|---|---|
| 発端 | PPC の syscall だけ io_pump の窓で EE を開ける案 (b) に対して「b であとあと他のアーキテクチャの追加で揺れていきそうですが。今はそれでいいですが、トータルでの解決策は考えておくべきでは？」 |
| 順序 | 「powerpc を待たせることで解決できるのであれば待たせたほうが良いです」→ P3 の実装を止めて、先にこの約束を決めて入れる |

## 1. 詳細設計 (確定) の要約
- ユーザーから入った syscall の本体は、全 arch で割り込みを許して走る (AC-CTX-1)。許可は arch の入口が持ち、I/O・待ちの口では操作しない。IRQ の出口で仕事を回さない。x86 は今の形で適合。
- PowerPC は syscall の dispatch の直前で EE を立て、出口で先に落とす (kernel_trap.c・kernel_vectors.S・kernel_dec.c)。IO_TIER の N4 (io_pump の窓) と LAN_BULK の J の PPC の窓は不要になる。
- 新しい arch の適合表に「共通・ドライバ・platform の I/O と待ちの口が IF/EE を立てていない」を入れ、arch を足しても窓が増えないことを毎回確かめる。
- 段の表は設計の §6 (W4a1〜W4h)。

## 2. 段の状態
| 段 | 状態 |
|---|---|
| W4a1・W4a2 (文書)、W4b (試験の土台) | 着手 |
| W4c〜W4h | 未着手 (W4c・W4f・W4g は同じ統合の枝) |
