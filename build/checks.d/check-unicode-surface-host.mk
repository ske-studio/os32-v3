CHECK_PAR_ORDER += 125:check-unicode-surface-host
# T2e e8a: two utf8 instances, RO system surfaces and real query/lease.
check-unicode-surface-host:
	$(call host32_check,test_unicode_surface.py)

.PHONY: check-unicode-surface-host
