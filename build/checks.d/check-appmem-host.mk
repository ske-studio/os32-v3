# T2f f2: unlinked extent preparation, real appmem.c (no kernel object).
CHECK_PAR_ORDER += 117:check-appmem-host
check-appmem-host:
	$(call host32_check,test_appmem.py)

.PHONY: check-appmem-host
