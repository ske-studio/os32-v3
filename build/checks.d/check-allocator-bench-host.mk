CHECK_PAR_ORDER += 151:check-allocator-bench-host
# T3c3: real nano/extent policy with guest measurement observer; no host perf.
check-allocator-bench-host:
	$(call host32_check,test_allocator_bench.py)
