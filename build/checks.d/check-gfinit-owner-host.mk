CHECK_PAR_ORDER += 199:check-gfinit-owner-host
check-gfinit-owner-host:
	python3 -B tools/tests/test_gfinit_owner.py $(MUT)
.PHONY: check-gfinit-owner-host
