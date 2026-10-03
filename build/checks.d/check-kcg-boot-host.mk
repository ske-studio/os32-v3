CHECK_PAR_ORDER += 131:check-kcg-boot-host
# T2e e8b: actual font loader, scratch closure and overlapping memory.
check-kcg-boot-host:
	$(call host32_check,test_kcg_boot.py)

.PHONY: check-kcg-boot-host
