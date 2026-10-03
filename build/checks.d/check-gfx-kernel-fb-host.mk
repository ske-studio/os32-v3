CHECK_PAR_ORDER += 121:check-gfx-kernel-fb-host
# T2e e4: real backend lifecycle, internal fb and DISPLAY lease.
check-gfx-kernel-fb-host:
	$(call host32_check,test_gfx_kernel_fb.py)

.PHONY: check-gfx-kernel-fb-host
