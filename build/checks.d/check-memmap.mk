CHECK_PAR_ORDER += 039:check-memmap
check-memmap:
	python3 tools/gen_memmap.py --check

.PHONY: check-memmap
