CHECK_PAR_ORDER += 136:check-shm-lockwrite-host
check-shm-lockwrite-host:
	$(call host32_check,test_shm_lockwrite.py)

.PHONY: check-shm-lockwrite-host
