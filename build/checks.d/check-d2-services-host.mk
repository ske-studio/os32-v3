CHECK_PAR_ORDER += 159:check-d2-services-host
# D2 A1 input providers, public wrappers and fixed boot wiring.
check-d2-services-host:
	$(call host32_check,test_d2_services.py)

.PHONY: check-d2-services-host
