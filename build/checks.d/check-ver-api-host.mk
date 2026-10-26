CHECK_PAR_ORDER += 198:check-ver-api-host
check-ver-api-host:
	$(call host32_check,test_ver_api.py)
.PHONY: check-ver-api-host
