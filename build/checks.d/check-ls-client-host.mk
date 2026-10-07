CHECK_PAR_ORDER += 156:check-ls-client-host
check-ls-client-host:
	$(call host32_check,test_ls_client.py)

.PHONY: check-ls-client-host
