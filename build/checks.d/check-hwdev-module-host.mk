CHECK_PAR_ORDER += 201:check-hwdev-module-host
check-hwdev-module-host:
	python3 -B tools/gen_hwdev_contract.py --check --cc $(CROSS_DIR)/bin/i386-elf-gcc --flags '$(filter-out -MMD -MP,$(KERNEL_CFLAGS))' --includes '$(INC_KERNEL)'
	python3 -B tools/tests/test_hwdev_module.py $(MUT)
.PHONY: check-hwdev-module-host
