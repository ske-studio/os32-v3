# T2g g1: real kernel trim request/DONE and legacy memory snapshot contract.
CHECK_PAR_ORDER += 120:check-trim-kernel-host
check-trim-kernel-host:
	$(call host32_check,test_trim_kernel.py)

.PHONY: check-trim-kernel-host
