CHECK_PAR_ORDER += 220:check-gf-production-host
check-gf-production-host:
	python3 -B tools/tests/test_gf_production.py
.PHONY: check-gf-production-host
