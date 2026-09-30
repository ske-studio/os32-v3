# vmkernel_lz4 検証記録 (LZ4 高圧縮と展開側 3 実装)

票: [TASK_SERIAL_HOSTFS](../../docs/archive/realhw_v21/TASK_SERIAL_HOSTFS.md) 部品 A-1
(先行して着地)。生成側の上限検査は [TASK_HDD_INSTALL](../../docs/archive/realhw_v21/TASK_HDD_INSTALL.md) N8。

## この文書の性格

`tools/mkvmkernel.py` の変更 (HC level 12 + 上限検査) を先に書き、試験は後から足した。
RED の根拠は**変異 13 件がすべて RED になること** (下の表) で、変更前のコードで
試験を回した RED→GREEN の経過ではない ([V4])。

## 実行 (2026-09-24 実測)

```
python3 -B tools/tests/test_vmkernel_lz4.py --real --target   # PASS
python3 -B tools/tests/test_vmkernel_lz4.py --target --mutate # 13 件すべて RED
make check-vmkernel-lz4-host                                  # 上の 2 つを 1 回で
```

ハーネス `vmkernel_lz4_host.c` は libc なしの 32bit 静的 ELF (int 0x80 で入出力)。
`boot/lz4_mini.c` (HDD ローダ、`-Os`) と `lib/lz4.c` (`-O0`) は `--target` で
**ローダと同じ i386-elf-gcc** で組む。FD ローダの `pm_lz4_decode` は
`boot/loader_fat_new.asm` から**そのまま切り出して** `nasm -f elf32` で組むので、
ASM も実物を 32bit で実行している。展開先の容量はローダと同じ raw ちょうどで、
後ろ 4KB の番兵を書き越したら失敗にする (ASM は境界を見ないため)。

## 実測 (build/out、2026-09-24、HEAD 47435ae + この変更)

| | 既定 (fast) | HC level 9 | HC level 12 |
|---|---|---|---|
| kernel (raw 293,832) | 210,296 | 179,505 | 179,132 |
| sqlite (raw 374,840) | 306,387 | 258,519 | 257,989 |
| vmkernel.lz4 | 516,731 | 438,072 | **437,169** |
| 上限 520,192 まで | 3,461 | 82,120 | **83,023** |

実イメージの流れ: kernel は 29,790 系列・offset 最大 65,532・重なるマッチ 68、
sqlite は 38,628 系列・offset 最大 65,529・延長 255 を跨ぐリテラル 2 / マッチ 1。

## 検査している振る舞い

| ケース | 内容 |
|---|---|
| synthetic | 合成データで VK32 を作り、3 実装とも raw ちょうどを返してバイト一致。延長 255 を跨ぐリテラル長・マッチ長、offset >= 32768、重なるマッチを**含むことを数えて確かめる**。既定の圧縮より小さい (HC になっている) |
| real (`--real`) | build/out の kernel.bin / sqlite.bin で同じ一致。無ければ FAIL |
| oversize | 上限を超えると mkvmkernel が非 0 で終わり、古い出力も消す |
| boundary | 合計がちょうど 520,192 は通り、520,193 は落ちる |

## 変異 (すべて RED)

lz4_mini.c / lib/lz4.c のそれぞれで「リテラル長の延長を打ち切る」「マッチ長の延長を
打ち切る」「offset の上位バイトを捨てる」、ASM で同じ 3 つ (`je .lz4_lit_ext` →
`nop`、`je .lz4_match_ext` → `nop`、`movzx eax, word` → `byte`)、mkvmkernel で
「上限を見ない」「上限を 1 バイト緩める」「fast に戻す」「超えたとき古い出力を残す」。

## 未検証

- NP21/W と実機での起動 (HDD ローダ・FD ローダとも)。ホストでは同じデコーダを
  同じ入力で回しただけで、ローダの読み込み・DF の状態・実メモリへの展開は見ていない。
- カーネル内の Rust デコーダ (`lib/os32_lz4`) は vmkernel.lz4 を展開しないので対象外。
