CHECK_PAR_ORDER += 039:check-memmap
check-memmap:
	python3 tools/gen_memmap.py --check
	python3 tools/gen_memmap.py --manifest --check

.PHONY: check-memmap
