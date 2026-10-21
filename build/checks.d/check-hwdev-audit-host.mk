CHECK_PAR_ORDER += 200:check-hwdev-audit-host
check-hwdev-audit-host:
	python3 -B tools/hwdev_audit.py --out $(BUILD_OUT) > $(BUILD_OUT)/hwdev-audit.json
.PHONY: check-hwdev-audit-host
