CHECK_PAR_ORDER += 031:check-ne2000-dma-host

# Real Remote DMA retry paths with host port I/O and PIO seams.
check-ne2000-dma-host:
	python3 -B tools/tests/test_ne2000_dma.py $(MUT)

.PHONY: check-ne2000-dma-host
