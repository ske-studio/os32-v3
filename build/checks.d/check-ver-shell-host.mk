CHECK_PAR_ORDER += 197:check-ver-shell-host
check-ver-shell-host:
	$(call host32_check,test_ver_shell.py)
.PHONY: check-ver-shell-host
