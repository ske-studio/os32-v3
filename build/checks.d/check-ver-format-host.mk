CHECK_PAR_ORDER += 195:check-ver-format-host
check-ver-format-host:
	$(call host32_check,test_ver_format.py)
.PHONY: check-ver-format-host
