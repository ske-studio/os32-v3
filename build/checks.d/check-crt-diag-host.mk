CHECK_PAR_ORDER += 129:check-crt-diag-host
# Read-only CRT snapshot, formatting bounds and complete bootlog capture.
# 記録: tools/tests/crt_diag_tdd.md
check-crt-diag-host:
	python3 -B tools/tests/test_crt_diag.py $(MUT)

.PHONY: check-crt-diag-host
