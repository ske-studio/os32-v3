CHECK_PAR_ORDER += 126:check-unicode-lookup-host
# T3d0: disposable candidate only; no product source or API changes.
check-unicode-lookup-host:
	$(call host32_check,test_unicode_lookup.py)

.PHONY: check-unicode-lookup-host
