# TASK_HPT370 — HighPoint HPT370 (PCI の IDE RAID カード) で大きな HDD と DVD マルチドライブを使う

> 状態: **計画 (2026-11-05)** — 読むだけの調べの台本 `userland/tests/hw/hpt370_probe.hwb` (HW-BASIC) を用意した段階。実機で流した結果を見てから設計の往復に出す。
> 上位: [RA266](../../decisions/realhw/RA266.md) §1-1 (0:12.0 1103:0004 rev 03、irq 5、io 0x6020/0x6028/0x6030/0x602c/0xc000)。道具: [HW_BASIC_DESIGN](../../decisions/realhw/HW_BASIC_DESIGN.md)。

## 0. ユーザー決定 (2026-11-05)

- 目的: 「大きな HDD を全容量で使う」「速く読み書きする (UDMA)」。「現在何もつながっていない。DVDマルチドライブが乗る予定。あとハードディスクも。」— RAID の配列は目的に入っていない。
- 調べの台本は PM が書いてよい (「PM が書く (推奨)」)。

## 1. 分かっていること

| 項目 | 値・出典 |
|---|---|
| チップ | 1103:0004 rev 03 = **HPT370 (UDMA5 = ATA/100、2 チャネル)** — FreeBSD `sys/dev/ata/chipsets/ata-highpoint.c:68-85` (BSD-2-Clause、PM の手元 `~/os32-tmp/ref/freebsd-main/sys/dev/ata/`、2026-11-05 取得) |
| 初期化 | 設定空間 0x51・0x55 の bit0-1 を落とす (割り込みの予測を切る)、0x5a の bit4 を落とす (割り込みを有効)、0x5b ← 0x22 (クロック、HPT370) — 同 `ata-highpoint.c:104-135`。その後は普通の ATA (コマンド +0..+7、制御 +2、バスマスタ +0/+2/+4) |
| BAR | BIOS 割当済み: cmd0 0x6020・ctl0 0x6028・cmd1 0x6030・ctl1 0x602c・BM 0xc000 (RA266 §1-1) |
| 起動 | PC-98 の BIOS は PCI カードの AT 用 ROM を走らせないので、起動は今どおり内蔵 IDE か FD から。HPT370 の先のドライブは OS32 のドライバが起動の後に使う |
| 今の HDD | 内蔵 IDE の Maxtor 6L300R0 は約 8GB として見えている (RA266 §1-1) |

## 2. 次の一手

1. 実機で `hw run /test/hw/hpt370_probe.hwb` — 設定空間 00-7F、両チャネルの状態、バスマスタの状態 (何もつながっていなければ IDENTIFY は送らない)。ドライブをつないだら同じ台本で型番・容量 (LBA28/LBA48)・UDMA を読む。
2. 結果を見て設計の往復 (astra 起草、Opus レビュー、手本 FreeBSD の ata-highpoint・ata-dma・ata-all、[HW3])。範囲の案: PIO と LBA48 の読み書き → バスマスタ DMA (UDMA5、[HW2] の 64KB 境界) → ATAPI (DVD、既存の CD の iso9660 につなぐ)。
