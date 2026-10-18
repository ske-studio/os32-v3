CHECK_PAR_ORDER += 154:check-fep-boundary-host
# T5a-1: B2 copied inputs, staged output and borrowed binding lifetime.
check-fep-boundary-host:
	$(call host32_check,test_fep_boundary.py)

.PHONY: check-fep-boundary-host
