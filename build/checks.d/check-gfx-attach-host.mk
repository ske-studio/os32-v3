CHECK_PAR_ORDER += 123:check-gfx-attach-host
# T2e e6: checked C SDK attach and dormant compatibility USER bridge.
check-gfx-attach-host:
	$(call host32_check,test_gfx_attach.py)

.PHONY: check-gfx-attach-host
