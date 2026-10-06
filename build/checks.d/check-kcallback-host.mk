CHECK_PAR_ORDER += 138:check-kcallback-host
check-kcallback-host:
	$(call host32_check,test_kcallback.py)

.PHONY: check-kcallback-host
