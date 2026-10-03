CHECK_PAR_ORDER += 112:check-caller-copy-host
# T2d d4: bounded copies through the real managed walk.
check-caller-copy-host:
	$(call host32_check,test_caller_copy.py)

.PHONY: check-caller-copy-host
