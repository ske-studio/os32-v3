CHECK_PAR_ORDER += 193:check-ver-state-name-host
check-ver-state-name-host:
	$(call host32_check,test_ver_state_name.py)
.PHONY: check-ver-state-name-host
