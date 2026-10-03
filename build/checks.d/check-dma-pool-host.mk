CHECK_PAR_ORDER += 053:check-dma-pool-host
# DMA プールの表 (kernel/dma_pool_math.c)。票 TASK_HAL_WIRING §1-3。
# 池は 0x2E8000〜0x2F7FFF で **0x2F0000 の 64KB 境界を跨ぐ**ので、跨ぐ候補を
# 飛ばして後半に置けること・33KB が空の池でも必ず失敗すること・隣接 span の
# 解放が先頭一致でだけ通ること・LEAKED を再利用しないことを見る。
# --target は i386-elf で唯一の池 (dma_pool.c) ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/dma_pool_tdd.md。
check-dma-pool-host:
	python3 -B tools/tests/test_dma_pool.py --target $(MUT)

.PHONY: check-dma-pool-host
