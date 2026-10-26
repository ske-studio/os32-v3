CHECK_PAR_ORDER += 194:check-ver-services-host
check-ver-services-host:
	$(call host32_check,test_ver_services.py)
.PHONY: check-ver-services-host
