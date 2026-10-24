CHECK_PAR_ORDER += 065:check-sh-exec-host
# TT4: preserve script arguments, controls and mutations; share the jobserver.
check-sh-exec-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --control tools/tests/test_sh_launch.py \
	    --control tools/tests/test_sh_shell.py

.PHONY: check-sh-exec-host
