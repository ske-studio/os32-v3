# T2g g3: real kernel/nano trim with cooperative WM delivery and retry.
CHECK_PAR_ORDER += 120:check-trim-flow-host
check-trim-flow-host:
	$(call host32_check,test_trim_flow.py)

.PHONY: check-trim-flow-host
