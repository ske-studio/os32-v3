CHECK_PAR_ORDER += 155:check-h4-backing-host
# TASK_T3_LAYOUT T3b2: actual kdebug high-backing fixture and failure cleanup.
check-h4-backing-host:
	$(call host32_check,test_h4_backing.py)

.PHONY: check-h4-backing-host
