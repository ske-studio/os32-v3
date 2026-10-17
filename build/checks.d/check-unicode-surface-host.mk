CHECK_PAR_ORDER += 125:check-unicode-surface-host
# T3d1: kernel-owned Unicode, SDK KAPI conversion, reserved-role refusal and TVRAM.
check-unicode-surface-host:
	$(call host32_check,test_unicode_surface.py)

.PHONY: check-unicode-surface-host
