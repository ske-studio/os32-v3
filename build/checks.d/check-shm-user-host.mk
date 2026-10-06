CHECK_PAR_ORDER += 132:check-shm-user-host
# T2e e10a: boot/shared USER, session unwind, exact low PTE/PDE restore.
check-shm-user-host:
	$(call host32_check,test_shm_user.py)

.PHONY: check-shm-user-host
