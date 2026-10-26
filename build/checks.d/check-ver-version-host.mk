CHECK_PAR_ORDER += 191:check-ver-version-host
check-ver-version-host:
	$(call host32_check,test_ver_version.py)
.PHONY: check-ver-version-host
