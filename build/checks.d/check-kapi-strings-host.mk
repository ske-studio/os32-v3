CHECK_PAR_ORDER += 159:check-kapi-strings-host
check-kapi-strings-host:
	$(call host32_check,test_kapi_strings.py)

.PHONY: check-kapi-strings-host
