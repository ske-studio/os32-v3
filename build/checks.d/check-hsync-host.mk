CHECK_PAR_ORDER += 070:check-hsync-host
# TT4: preserve script arguments, controls and mutations; share the jobserver.
check-hsync-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --command "tools/tests/test_hsync_h1.py --target" \
	    --command "tools/tests/test_hsync_h2.py --target $(MUT)" \
	    --command "tools/tests/test_hsync_h3.py --target $(MUT)"

.PHONY: check-hsync-host
