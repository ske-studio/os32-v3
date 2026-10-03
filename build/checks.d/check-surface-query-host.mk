CHECK_PAR_ORDER += 114:check-surface-query-host
# T2e e1: dormant query/authorization through real B1 and managed paging.
check-surface-query-host:
	$(call host32_check,test_surface_query.py)

.PHONY: check-surface-query-host
