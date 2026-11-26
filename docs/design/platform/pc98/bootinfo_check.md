# sys/platform/pc98/bootinfo_check.c — 設計
> 状態: **現行 (2026-11-18)** — 出所: TASK_HDD_INSTALL 段 0 (§1-v3 N1・N2) の契約を今のソースと照らして要約
ソース: `sys/platform/pc98/bootinfo_check.c` (`bootinfo_parse`・`bootinfo_drive_usable`・`bootinfo_sum`・`bootinfo_seal`・`bootinfo_seal_image`・表示行)・形式 `sys/platform/pc98/bootinfo_wire.h`・誤りコード `sys/platform/pc98/bootinfo_native.h`・写す側 `sys/platform/pc98/bootinfo_capture.c` (`bootinfo_capture`)・ローダの手続き `boot/bootinfo_rm.inc`・NASM の写し `boot/bootinfo.inc`

ローダ (FD・HDD) が実モードで INT 1Bh AH=84h を呼んだ結果を、ブート情報域 (`MEM_BOOTINFO_BASE` = 0x7E00、予約 256B、
`sys/include/memmap.h`) に書く。カーネルは最初に写して検証し、検証を通った値だけを BIOS 幾何として使う。
0x7E00 はカーネルが後で別の用途に使う帯の中なので、検証を通らない値 (ゴミ・書きかけ・前回の残り) を幾何と読むと、
区画表が別のシリンダ境界に書かれる。

## 形式 (リトルエンディアン、`BOOTINFO_WIRE_SIZE` = 0x40)

| オフセット | 大きさ | 中身 |
|---|---|---|
| +0x00 | u32 | magic `BOOTINFO_MAGIC` = 0x49544F42 (バイト列 `42 4F 54 49`) |
| +0x04 | u16 | 版 `BOOTINFO_VERSION` = 2 |
| +0x06 | u8 | 取得元 1 = FD ローダ / 2 = HDD ローダ |
| +0x07 | u8 | ドライブ数 = 2 |
| +0x08・+0x18 | 16B × 2 | ドライブ記録 (DA 80h → [0]、81h → [1]): +0 DA、+1 ローダの valid、+2 CF、+3 AH、+4 BX (u16)、+6 CX (u16)、+8 DH、+9 DL、+10 queried、残り 0 |
| +0x28 | u16 | ドライブ記録 (+0x08〜+0x27) のバイト和 |
| +0x2C | u32 | 反転チェック語 `BOOTINFO_CHECK` = ~(magic ^ 版) (バイト列 `BC B0 AB B6`)。**主部で最後に書く** |
| +0x30 | u32 | 起動したイメージ (VK32) のファイル全体の CRC32 |
| +0x34 | u32 | 読んだイメージの長さ |
| +0x38 | u32 | イメージ欄のチェック語 = crc ^ size ^ `BOOTINFO_IMG_KEY` ('IMGC')。**イメージ欄で最後に書く** |

構造体 `bootinfo_wire` の並びとオフセット定数の一致は `bootinfo_capture.c` の `STATIC_ASSERT` が見張る。
NASM の写し `boot/bootinfo.inc` の EQU は C の定義と名前ごとに一致させる (試験 `inc_mirror`)。

## 検証 (`bootinfo_parse`)

次の順で見て、最初に外れたものを返す。`out` は先に 0 で埋める。

| 条件 | 戻り値 |
|---|---|
| `raw` が NULL / 長さ < `BOOTINFO_WIRE_SIZE` | `BOOTINFO_ERR_ARG` (-1) |
| magic が違う (0・全 0・全 FF を含む) | `BOOTINFO_ERR_MAGIC` (-2) |
| 反転チェック語が違う (0 = magic まで書いて止まった、1 ビット違い) | `BOOTINFO_ERR_CHECK` (-3) |
| 版が 2 でない (1 も 3 も断る) | `BOOTINFO_ERR_VERSION` (-4) |
| ドライブ数が 2 でない | `BOOTINFO_ERR_NDRIVES` (-6) |
| ドライブ記録の和が違う (封の後の部分的な上書き) | `BOOTINFO_ERR_SUM` (-5) |

まだ写していない間の状態は `BOOTINFO_ERR_NONE` (-7)。

**1 ドライブが使えるか**: ローダの valid が 1 **かつ** カーネル側の規則 (`bootinfo_drive_usable`) を満たすときだけ
`valid` = 1。ローダが 0 と書いたものを拾い直さず、ローダの 1 も鵜呑みにしない。規則: queried = 1、DA が 80h か 81h、
CF = 0 (取得失敗の CF = 1 は使わない)、BX (セクタ長) = 512 (SASI の 256B・CD の 2048B を断る)、CX・DH・DL がどれも 0 でない
(0 で割らない。NP21/W の空きスロットは全部 0)。

**イメージ欄**: 主部が OK のときだけ見る。長さが 0 でなく、チェック語が合うときだけ `img_valid` = 1。
合わなければ「記録なし」で、主部の状態は OK のまま (幾何は使える)。長さ 0 は crc = size = 0 でチェック語が KEY に
一致する偶然を拾わないために断る。

## 写し方 (`bootinfo_capture`)

起動の最初の段で 1 回だけ、域の `BOOTINFO_WIRE_SIZE` バイトを写して検証し、**低位の magic を 0 に戻す**
(次の起動で域を書かない古いローダから来ても、今回の値を読まない)。以後は写しだけを使う。

## ローダの側 (`boot/bootinfo_rm.inc`)

FD ローダと HDD ローダの両方が同じ手続き `bi_clear` → `bi_sense` → `bi_seal` を使う。起動のたびに域をまず無効 (magic = 0)
で初期化してから問い合わせ、反転チェック語を最後に書く。FD ローダは 80h と 81h、HDD ローダは IPL から受けた DA だけを
問い合わせる (問い合わせなかった方は queried = 0)。HDD ローダは AH=84h の DH/DL が IPL の heads/SPT と違えば画面に
`HDD geom mismatch` を出して止まり、AH=84h が使えない答えなら比べずに進む。

## 表示行 (`BOOTINFO_LINE_MAX` 以内、小さい buf でも終端する)

```
[hdd] bios da=80 cf=0 ah=00 len=512 C/H/S=16382/16/63 src=fd
[hdd] bios da=81 cf=1 ah=60 len=0 C/H/S=0/0/0 src=fd (unusable)
[hdd] bios geom: none (magic xxxxxxxx err=-N)
[hdd] ata0 def=C/H/S cur=C/H/S(valid) lba=1 total=N
```

使えない記録には ` (unusable)`、問い合わせていない記録には ` not queried` を付ける。ATA の行の `cur=` は IDENTIFY の
word 53 bit0 が立っていれば `(valid)`、無ければ `(n/a)`。

## 確かめ

- `make check-bootinfo-host` (`test_bootinfo.py (旧版 307f0c73339c)`)。ケース `good`・`magic`・`check_word`・`version`・`sum`・
  `rules`・`format`・`image`・`inc_mirror`。`--target` で `sys/kern/init/bootinfo.c` と `sys/dev/ata/ide.c` を i386-elf で組む。
- ローダの実モードの手続き (域へ書くこと、書く順序、HDD ローダの幾何の食い違いでの停止) はホストでは見られない。
  起動時は kselftest の `test_bootinfo()` が写したこと・magic を消したことを見る。
