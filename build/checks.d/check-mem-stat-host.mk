# f13: read-only memory statistics and shell output.
CHECK_PAR_ORDER += 119:check-mem-stat-host
check-mem-stat-host:
	$(call host32_check,test_mem_stat.py)

.PHONY: check-mem-stat-host
