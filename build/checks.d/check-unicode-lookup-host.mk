CHECK_PAR_ORDER += 126:check-unicode-lookup-host
# T3d1: generated pair file + product loader/lookup, with a mock SDK path.
check-unicode-lookup-host:
	$(call host32_check,test_unicode_lookup.py)

.PHONY: check-unicode-lookup-host
