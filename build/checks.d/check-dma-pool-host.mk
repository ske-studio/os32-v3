CHECK_PAR_ORDER += 053:check-dma-pool-host
# DMA プールの表 (mm/dma_pool_math.c)。票 TASK_HAL_WIRING §1-3。
# 実物の池 (MEM_DMA_POOL_*) は 64KB 整列。**64KB 境界を跨ぐ合成の池**
# (0x2E8000〜) で跨ぐ候補を飛ばして後半に置けること・33KB が空の池でも必ず失敗すること・隣接 span の
# 解放が先頭一致でだけ通ること・LEAKED を再利用しないことを見る。
# --target は i386-elf で唯一の池 (dma_pool.c) ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/dma_pool_tdd.md。
check-dma-pool-host:
	python3 -B tools/tests/test_dma_pool.py --target $(MUT)

.PHONY: check-dma-pool-host
