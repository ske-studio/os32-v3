CHECK_PAR_ORDER += 120:check-surface-bundle-host
# T2e e3: four-plane DISPLAY transaction and native exec/V86 cache lifetime.
check-surface-bundle-host:
	$(call host32_check,test_surface_bundle.py)

.PHONY: check-surface-bundle-host
