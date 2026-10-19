CHECK_PAR_ORDER += 091:check-font-reader-host
check-font-reader-host:
	python3 -B tools/tests/test_font_reader.py $(MUT)

.PHONY: check-font-reader-host
