CHECK_PAR_ORDER += 133:check-kapi-ranges-host
# kapinull: generated KAPI range guards (NULL output / full input range) with the
# real B1 managed USER walk and the real dispatcher early check.
# Run every explicit ILP32 control; mutate only the first runner. No fallback.
check-kapi-ranges-host:
	$(call host32_check,test_kapi_ranges.py)

.PHONY: check-kapi-ranges-host
