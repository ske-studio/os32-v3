CHECK_PAR_ORDER += 052:check-dma8237-host
# 8237 DMA 共通部の算数 (drivers/dma8237_math.c)。票 TASK_HAL_WIRING §1-2。
# 見るのは 4 つ: 64KB バンクまたぎの判定、16MB の壁、TC 後の FFFFh を弾く
# 安定読みの採用規則、**バンクレジスタが等差数列でないこと** (ch0 だけ
# 0027h。式で出すと ch1 のバンクを壊す)。
# **NP21/W は 8237 の折り返しも 16MB の壁も模擬しない**ので、またいだ転送が
# 「たまたま読めて」しまう (§4-51 と同じ型)。断る規則をホストで固定する。
# --target はカーネルと同じ i386-elf で I/O を出す側 (dma8237.c) ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/dma8237_tdd.md。
check-dma8237-host:
	python3 -B tools/tests/test_dma8237.py --target $(MUT)

.PHONY: check-dma8237-host
