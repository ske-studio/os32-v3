CHECK_PAR_ORDER += 141:check-kdebug-init-host
# 記録: tools/tests/kdebug_init_tdd.md
check-kdebug-init-host:
	python3 -B tools/tests/test_kdebug_init.py $(MUT)

.PHONY: check-kdebug-init-host
