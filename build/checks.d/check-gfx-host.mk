CHECK_PAR_ORDER += 121:check-gfx-host
# TT3: keep every script/control/mutation; recursive make shares the jobserver.
check-gfx-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --host32 tools/tests/test_gfx_kernel_fb.py \
	    --host32 tools/tests/test_gfx_reinit.py \
	    --host32 tools/tests/test_gfx_attach.py \
	    --host32 tools/tests/test_gfx_reattach.py

.PHONY: check-gfx-host
