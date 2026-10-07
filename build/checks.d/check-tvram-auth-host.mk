CHECK_PAR_ORDER += 135:check-tvram-auth-host
check-tvram-auth-host:
	$(call host32_check,test_tvram_auth.py)

.PHONY: check-tvram-auth-host
