CHECK_PAR_ORDER += 133:check-memmap-audit-host
check-memmap-audit-host:
	$(call host32_check,test_memmap_audit.py)
	$(call host32_check,test_e10c_lifecycle.py)
.PHONY: check-memmap-audit-host

# Named entry used by DBGREF acceptance; the suite already includes it.
check-e10c-lifecycle: check-memmap-audit-host
.PHONY: check-e10c-lifecycle
