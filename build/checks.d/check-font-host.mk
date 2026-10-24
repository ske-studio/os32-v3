CHECK_PAR_ORDER += 091:check-font-host
# TT3: keep every script/control/mutation; recursive make shares the jobserver.
check-font-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --plain tools/tests/test_font_reader.py \
	    --plain tools/tests/test_font_outline.py \
	    --plain tools/tests/test_font_raster.py \
	    --plain tools/tests/test_font_cache.py \
	    --plain tools/tests/test_font_engine.py

.PHONY: check-font-host
