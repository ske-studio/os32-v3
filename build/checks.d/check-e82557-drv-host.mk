CHECK_PAR_ORDER += 089:check-e82557-drv-host
# LB2 DMA/CU, stop proof and persistent attempt gate; real driver injection.
# Record: tools/tests/e82557_drv_tdd.md
check-e82557-drv-host:
	python3 -B tools/tests/test_e82557_drv.py --target $(MUT)

.PHONY: check-e82557-drv-host
