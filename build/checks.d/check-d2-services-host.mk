CHECK_PAR_ORDER += 159:check-d2-services-host
# D2 A/B input, pointer, port, console/emergency providers and public wrappers.
check-d2-services-host:
	$(call host32_check,test_d2_services.py)

.PHONY: check-d2-services-host
