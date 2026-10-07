CHECK_PAR_ORDER += 117:check-sbrk-crt-host
check-sbrk-crt-host:
	$(call host32_check,test_sbrk_crt.py)
