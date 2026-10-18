CHECK_PAR_ORDER += 153:check-fep-ro-host
# T4c-3: real CD FEP and product ILP32 memory admission cap.
check-fep-ro-host:
	$(call host32_check,test_fep_ro.py)

.PHONY: check-fep-ro-host
