CHECK_PAR_ORDER += 196:check-ver-collect-host
check-ver-collect-host:
	$(call host32_check,test_ver_collect.py)
.PHONY: check-ver-collect-host
