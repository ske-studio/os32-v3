CHECK_PAR_ORDER += 032:check-shlib

check-shlib:
	python3 tools/mkshlib.py --check

.PHONY: check-shlib
