CHECK_PAR_ORDER += 202:check-hwdev-image-host
check-hwdev-image-host:
	python3 -B tools/tests/test_hwdev_image.py $(MUT)
.PHONY: check-hwdev-image-host
