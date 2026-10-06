CHECK_PAR_ORDER += 134:check-disk-auth-host
check-disk-auth-host:
	$(call host32_check,test_disk_auth.py)

.PHONY: check-disk-auth-host
