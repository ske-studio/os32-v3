# ELF32 の再配置

> 上位: docs/ppc/README.md

以下は PowerPC ELF の再配置の識別と計算の事実。
OS32 のカーネル・モジュールが受理する集合の決定はこの文書では行わない。
S はシンボル値、A は addend、P は書込み位置、B はロード基底。
根拠は N/sys/arch/powerpc/include/elf_machdep.h:56 と同 :67 以降。

| R_PPC_ 名 | 番号 | 計算・用途 |
|---|---|---|
| NONE | 0 | 書込みなし |
| ADDR32 | 1 | `S+A` の 32 ビット絶対番地 |
| ADDR16 / ADDR16_LO | 3 / 4 | 16 ビット / 下位 16 ビット |
| ADDR16_HI | 5 | 上位 16 ビット |
| ADDR16_HA | 6 | 下位の符号拡張を補う上位 16 ビット |
| REL24 | 10 | `S+A−P` を分岐命令の変位へ。語単位のフィールド |
| REL14 | 11 | `S+A−P` の条件分岐変位。語単位のフィールド |
| GOT16 / GOT16_LO / GOT16_HI / GOT16_HA | 14〜17 | GOT を使う参照 |
| PLTREL24 | 18 | PLT を通る相対分岐 |
| COPY / GLOB_DAT / JMP_SLOT | 19 / 20 / 21 | 動的リンク向け |
| RELATIVE | 22 | `B+A` |
| UADDR32 / UADDR16 | 24 / 25 | 非整列位置の絶対値 |
| REL32 | 26 | `S+A−P` の 32 ビット値 |
| SDAREL16 | 32 | `S+A−_SDA_BASE_` |

`HA(x) = ((x >> 16) + ((x & 0x8000) != 0)) & 0xffff`。
単純な HI と違い、後続命令が下位即値を符号拡張する場合の繰上がりを含む。
[未] 分岐の範囲超過・整列違反・各フィールドの保存マスクの厳密な処理は原典で照合する。

FreeBSD の ELF32 カーネル再配置は REL を拒否し、RELA の明示 addend を読む
(F/sys/powerpc/powerpc/elf32_machdep.c:246)。
同実装の処理集合は NONE/ADDR32/ADDR16_LO/ADDR16_HA/RELATIVE/JMP_SLOT/
IRELATIVE (同 :262)。
NetBSD の kobj は ADDR16_HI、ADDR16、REL32 等も分類する
(N/sys/arch/powerpc/powerpc/kobj_machdep.c:101)。
この差は BSD ローダーの対応範囲であり、ELF の数値定義の矛盾ではない。
OS32 の必要集合は実際のコンパイラ・リンク方法で発生する relocation と合わせて
設計側で選ぶ。固定番地の実行像と未解決シンボルを持つモジュールを区別する。

[未] ELF32 のフラグ・セクション属性・構造体返却との関係、動的 PLT/TLS の全手順。
PowerPC Processor Supplement の ELF/relocation 章を取り寄せて補う。
