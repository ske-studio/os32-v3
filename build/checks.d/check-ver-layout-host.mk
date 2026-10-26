CHECK_PAR_ORDER += 192:check-ver-layout-host
check-ver-layout-host:
	$(call host32_check,test_ver_layout.py)
.PHONY: check-ver-layout-host
