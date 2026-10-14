CHECK_PAR_ORDER += 136:check-module-image-host
# 記録: tools/tests/module_image_tdd.md
check-module-image-host:
	python3 -B tools/tests/test_module_image.py $(MUT)

.PHONY: check-module-image-host
