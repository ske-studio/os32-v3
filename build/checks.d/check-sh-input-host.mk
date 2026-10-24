CHECK_PAR_ORDER += 067:check-sh-input-host
# TT4: preserve script arguments, controls and mutations; share the jobserver.
check-sh-input-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --plain tools/tests/test_sh_status.py \
	    --command "tools/tests/test_sh_truncation.py $(if $(mut_on),--mutate-rshell)"

.PHONY: check-sh-input-host
