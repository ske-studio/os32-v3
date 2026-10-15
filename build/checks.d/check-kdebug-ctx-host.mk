CHECK_PAR_ORDER += 142:check-kdebug-ctx-host
check-kdebug-ctx-host:
	python3 -B tools/tests/test_kdebug_ctx.py $(MUT)

.PHONY: check-kdebug-ctx-host
