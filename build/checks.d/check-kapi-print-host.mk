CHECK_PAR_ORDER += 158:check-kapi-print-host
check-kapi-print-host:
	$(call host32_check,test_kapi_print.py)

.PHONY: check-kapi-print-host
