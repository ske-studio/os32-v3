CHECK_PAR_ORDER += 194:check-recover-cli-host
check-recover-cli-host:
	python3 -B tools/tests/test_recover_cli.py $(MUT)
.PHONY: check-recover-cli-host
