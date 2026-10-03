CHECK_PAR_ORDER += 111:check-access-walk-host
# T2d d3: managed tables/PFN with real paging and allocator.
# Run every explicit ILP32 control; mutate only the first runner. No fallback.
check-access-walk-host:
	python3 -B tools/tests/test_host32.py
	$(call host32_check,test_access_walk.py)

.PHONY: check-access-walk-host
