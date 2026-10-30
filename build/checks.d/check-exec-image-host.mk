CHECK_PAR_ORDER += 037:check-exec-image-host
check-exec-image-host:
	python3 -B tools/tests/test_exec_image.py $(MUT)

.PHONY: check-exec-image-host
