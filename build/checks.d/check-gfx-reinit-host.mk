CHECK_PAR_ORDER += 122:check-gfx-reinit-host
# T2e e5: three ASes and real lifecycle/ledger/lease; every runner gets a control.
check-gfx-reinit-host:
	$(call host32_check,test_gfx_reinit.py)

.PHONY: check-gfx-reinit-host
