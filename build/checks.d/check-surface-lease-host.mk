CHECK_PAR_ORDER += 119:check-surface-lease-host
# T2e e2: single-surface USER lease and transactional B1 copyout rollback.
check-surface-lease-host:
	$(call host32_check,test_surface_lease.py)

.PHONY: check-surface-lease-host
