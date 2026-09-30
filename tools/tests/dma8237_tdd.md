# dma8237 — RED → GREEN の記録

票: [`docs/tasks/v3/TASK_HAL_WIRING.md`](../../docs/tasks/v3/TASK_HAL_WIRING.md) §1-2
対象: `drivers/dma8237_math.c` (算数とポート表) / `drivers/dma8237.c` (I/O を出す側)
試験: `tools/tests/test_dma8237.py` + `tools/tests/dma8237_host.c`
実行: `make check-dma8237-host` (`check-par` の列)

## 何が起きていたか

DMA の設定は `drivers/fdc.c` の中に **ch2 決め打ち**で書かれていた。

```c
outp(DMA_MASK_REG, DMA_MASK_CH2);       /* 0x15 <- 0x06 */
outp(DMA_MODE_REG, DMA_MODE_READ);      /* 0x17 <- 0x46 */
outp(DMA_CH2_ADDR, addr & 0xFF);        /* 0x09 */
outp(DMA_CH2_BANK, bank);               /* 0x23 */
```

CS4231 の PCM は #1/#3 を auto-init で回すので、この形のままでは足せない。
チャネルを引数に取る層へ移すにあたって、**装置ドライバが素通しで通していた
検査を明文化する**必要があった。検査が無いまま通っていたのは 3 つ:

1. **64KB バンクまたぎ** ([HW2])。8237 はバンクレジスタの下で 16bit の番地しか
   持たないので、またいだ転送は同じバンクの先頭へ**折り返す**。FDC は
   `FDC_DMA_ALIGN 1024` で揃えていたので踏まなかっただけ。
2. **16MB の壁**。バンクレジスタは 8bit。拡張バンク (0E05h〜) はこの票の外。
3. **`bytes == 0`**。積むのは「バイト数 - 1」なので、0 を渡すと 0xFFFF =
   **65536 バイト転送**になる。

どれも **NP21/W では踏めない** — エミュレータは折り返しも 16MB の壁も模擬
しないので「またいでも読めて」しまう (§4-49・§4-51・§4-53 と同じ型)。

もうひとつ、**バンクレジスタのポートが等差数列ではない**。UNDOCUMENTED
Vol.2「DMAコントローラ」(io_dma.md 152〜155 行) は

    0021h  DMAチャネル1 バンクアドレス
    0023h  DMAチャネル2 バンクアドレス
    0025h  DMAチャネル3 バンクアドレス
    0027h  DMAチャネル0 バンクアドレス   ← **ch0 だけ末尾に飛ぶ**

と並ぶ。`0x1F + 2*ch` の式で出すと ch0 が 0x1F (存在しないポート) になり、
`0x21 + 2*ch` にすると ch0 が **ch1 のバンクを壊す**。表で持つしかない。

## RED

### RED 1 — 実装が無い

`drivers/dma8237_math.c` を置く前にハーネスを回すと `#include` が解決できない。

```
$ python3 -B tools/tests/test_dma8237.py
tools/tests/dma8237_host.c:16:10: fatal error: ../../drivers/dma8237_math.c: No such file or directory
```

### RED 2 — ポート表を式で出した姿 (変異 5)

`{ DMA8237_CH0_ADDR, DMA8237_CH0_COUNT, DMA8237_CH0_BANK }` の ch0 のバンクを
`DMA8237_CH1_BANK` に差し替える = 等差数列で出した実装。`port_table` が落ちる。
**実機では ch0 の転送が ch1 のバンクを書き換えてから走る**ので、同時に動いて
いる装置のほうが壊れる — 症状が原因から遠い型のバグ。

### RED 3 — 64KB またぎを見ない (変異 8)

`dma_check_args` の `dma_crosses_64k` を消す = [HW2] そのもの。`check_args` と
`crosses` が落ちる。エミュレータでは最後まで通る。

## GREEN

```
$ python3 -B tools/tests/test_dma8237.py --target --mutate
HOST GNU89 -Werror compile PASS (real drivers/dma8237_math.c)
TARGET i386-elf GNU89 -Werror PASS
EXIT port_table=0
EXIT split_addr=0
EXIT crosses=0
EXIT count_bytes=0
EXIT accept_pair=0
EXIT check_args=0
EXIT mode_bytes=0
SUMMARY 7/7 PASS
MUTATION 1 RED (1 件): 0 バイトを「またがない」と答える (積むと 65536 バイト転送になる)
MUTATION 2 RED (1 件): またぎ判定が 1 バイトぶん緩い (末尾 1 バイトが別バンクへ折り返す)
MUTATION 3 RED (compile): 設定長を超えた読みを採用する (TC 後の FFFFh を残量と読む)
MUTATION 4 RED (1 件): 増えたカウントを採用する (再ロードをまたいだ合成を残量と読む)
MUTATION 5 RED (1 件): ch0 のバンクを等差数列で出す (0027h ではなく 0021h = ch1 を壊す)
MUTATION 6 RED (1 件): 16MB の壁を見ない (バンクレジスタ 8bit では届かない番地へ積む)
MUTATION 7 RED (1 件): 転送の向きを取り違える (読んだつもりで書き、ディスクを潰す)
MUTATION 8 RED (1 件): 64KB またぎを検査しない ([HW2] そのもの)
```

## 何を見ているか

| ケース | 見るもの |
|---|---|
| `port_table` | 8 本のアドレス / カウントと 4 本のバンク。**ch0 のバンクだけ式から外れる**こと。範囲外は表を引かずに負で、出力を触らない |
| `split_addr` | 物理 → (16bit オフセット, 8bit バンク)。プールの先頭 0x2E8000 と 16MB-1 の両端 |
| `crosses` | バンクの先頭から 64KB ちょうどは通る / 1 バイト溢れは断る / 末尾 1 バイトは通る。**0 バイトは「またぐ」側**。プールが跨ぐ 0x2F0000 の前後 |
| `count_bytes` | `count + 1` ⇄ `bytes - 1` の往復 |
| `accept_pair` | 一致 / 64 以内の減り = 採用、**増加**と設定長超えは不採用。境界 (`limit` ちょうど、0) |
| `check_args` | ch 0〜3、dir / mode の値、bytes 1〜65536、64KB またぎ、**16MB を終端で**見ること |
| `mode_bytes` | 直す前の fdc.c の 0x46 / 0x4A を数で固定。auto-init で bit4。**向きは 8237 の呼び方と逆** (装置 → メモリ = ライト転送) |

## 変異を置かなかったところ

`dma_check_args` の **bytes 範囲 (1〜65536)** と **16MB の終端**は、どちらも
`dma_crosses_64k` が結果的に覆う (64KB を超える転送は必ずバンクをまたぐし、
16MB を越える終端も必ずまたぐ)。片方だけ消しても挙動が変わらないので、
16MB は**先頭と終端の 2 本をまとめて**消す変異にしてある。bytes 範囲は
契約を入口で言うための冗長な検査として残す — 消しても RED にならないことを
ここに書いておく。

## エミュレータでも実機でも見るしかない残り

ここは算数だけ。**I/O の順序** (フリップフロップのクリア → 下位 → 上位、
ステータスの読みが TC を消すこと) は `drivers/dma8237.c` にあり、
`kernel/kselftest.c` の `test_dma8237()` が起動ごとに「悪い引数で
ハードウェアに触らない」ことと done / tc_event の初期化を見る。
実転送の照合は W2 (NP21/W の 2HD 起動 + md5、実機の FD 起動)。
