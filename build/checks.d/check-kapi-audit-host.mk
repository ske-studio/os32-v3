CHECK_PAR_ORDER += 001:check-kapi-audit-host
# Real source guards, STOP safe points, and generated rshell authority.
check-kapi-audit-host:
	python3 -B tools/tests/test_kapi_audit.py $(MUT)
