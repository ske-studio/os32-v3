# bootinfo — RED → GREEN の記録

票: [`docs/archive/realhw_v21/TASK_HDD_INSTALL.md`](../../docs/archive/realhw_v21/TASK_HDD_INSTALL.md) 段 0 (§1-v3 N1 / N2)
対象: `kernel/bootinfo_check.c` (検証と表示行) / `boot/bootinfo.inc` (NASM 側の写し) / `kernel/bootinfo.c` (写す側)
試験: `tools/tests/test_bootinfo.py` + `tools/tests/bootinfo_host.c`
実行: `make check-bootinfo-host` (`check-par` の列)

## 何を作ったか

HDD へ区画を切る前に、**BIOS が 8GB ディスクをどの幾何で見せるか** (INT 1Bh AH=84h)
を実機で測る (票 F5)。ローダ 2 本 (FD / HDD) が実モードで AH=84h を呼び、結果を
物理 0x7E00 のブート情報域に書く。カーネルは `kernel_main` の最初で写し、magic・
反転チェック語・版・ドライブ記録の和を検証してから使う。0x7E00 はフォントキャッシュの
内側なので、検証を通らない値 (ゴミ・書きかけ・前回の残り) を幾何と読むと、段 1 の
区画表が別のシリンダ境界に書かれる。

## RED

### RED 1 — 検証の前の姿 (magic だけ見る)

`bootinfo_check.c` から反転語・版・ドライブ数・和の検査を外し、`bootinfo_drive_usable`
を「NULL でなければ使える」にした版 (= 0x7E00 の値を magic だけ見てそのまま使う実装) を
一時ディレクトリに置いてハーネスを回す:

```
EXIT good=0
EXIT magic=0
EXIT check_word=1
EXIT version=1
EXIT sum=1
EXIT rules=1
EXIT format=0
```

書きかけ (magic まで書いて check の前に止まった域)、封の後の部分的な上書き、SASI 256B /
CD 2048B / NP21/W の空きスロット (BX=CX=DH=DL=0) を全部「使える幾何」として通す。

## GREEN

```
$ python3 -B tools/tests/test_bootinfo.py --target --mutate
HOST GNU89 -Werror compile PASS (real kernel/bootinfo_check.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT good=0
EXIT magic=0
EXIT check_word=0
EXIT version=0
EXIT sum=0
EXIT rules=0
EXIT format=0
EXIT inc_mirror=0
SUMMARY 8/8 PASS
MUTATION 1 RED (1 件): 反転チェック語を見ない (書きかけの域を受け入れる)
MUTATION 2 RED (1 件): BX (セクタ長) を見ない (SASI 256B / CD 2048B を 512 として使う)
MUTATION 3 RED (1 件): DH = 0 を通す (0 で割る)
MUTATION 4 RED (1 件): DL = 0 を通す (0 で割る)
MUTATION 5 RED (1 件): ドライブ記録の和を見ない (部分的な上書きを通す)
MUTATION 6 RED (1 件): 版を見ない
MUTATION 7 RED (1 件): CF = 1 (取得失敗) を使える扱いにする
MUTATION 8 RED (1 件): ローダの valid を鵜呑みにする
MUTATION 9 RED (1 件): ローダが 0 と書いたものを拾い直す
MUTATION 10 RED (1 件): 問い合わせていないドライブを使う
INC MUTATION 1 RED: check の位置を sum に重ねる
INC MUTATION 2 RED: 反転語の値が 1 ビット違う
INC MUTATION 3 RED: DL を DH と同じ位置に書く
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `good` | 実機想定の域 (80h = 16382/16/63、81h = CF=1 AH=60h) を通す。封の値 (magic `42 4F 54 49`、check `BC B0 AB B6`) を**数で**固定 |
| `magic` | magic 0 (ローダが書いていない / 起動のたびの初期化で止まった)、全 0、全 FF (フォントのゴミ) を断る |
| `check_word` | check が 0 (magic まで書いて止まった) / 1 ビット違いを断る |
| `version` | 版 2、ドライブ数 3 を断る |
| `sum` | 封の後にドライブ記録が変わった域を断る |
| `rules` | 1 ドライブの規則: queried / DA 80h・81h / CF=0 / BX=512 / CX・DH・DL ≠ 0 / ローダの valid。NP21/W の NHD (8/17) は通し、空きスロット (全 0) は断る |
| `format` | 起動画面の `[hdd] bios …` / `[hdd] bios geom: none …` / `[hdd] ataN …` の書式と、小さい buf での終端 |
| `inc_mirror` | `boot/bootinfo.inc` の EQU が `include/bootinfo.h` と名前ごとに一致 (番地 `MEM_BOOTINFO_BASE` は `gen_memmap.py --check`) |

`--target` は i386-elf で `kernel/bootinfo.c` (構造体 `bootinfo_wire` の並びと
オフセット定数の一致を `STATIC_ASSERT`) と `drivers/ide.c` を通す。

## ホストで見られない残り

- ローダの実モードの手続き (`boot/bootinfo_rm.inc` の `bi_clear` / `bi_sense` / `bi_seal`)
  が実際に 0x7E00 へ書くこと、書く順序 (check が最後)、HDD ローダの幾何食い違いでの停止と
  読みの CF 検査。これは NP21/W と実機でしか踏めない (受入 H2 / H3)。
- 起動時は `kernel/kselftest.c` の `test_bootinfo()` が「写したこと」「低位の magic を
  消したこと」「良い域を通し check の壊れた域を断ること」を見る。
