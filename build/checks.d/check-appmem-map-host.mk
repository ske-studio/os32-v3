# T2f f3: unlinked map transaction, real paging/allocator/physmem (no kernel object).
CHECK_PAR_ORDER += 118:check-appmem-map-host
check-appmem-map-host:
	$(call host32_check,test_appmem_map.py)

.PHONY: check-appmem-map-host
