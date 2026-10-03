CHECK_PAR_ORDER += 124:check-gfx-reattach-host
# T2e e7: both SDK instances over real CLIENT query/lease and three backends.
check-gfx-reattach-host:
	$(call host32_check,test_gfx_reattach.py)

.PHONY: check-gfx-reattach-host
