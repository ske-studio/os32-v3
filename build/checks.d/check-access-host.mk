CHECK_PAR_ORDER += 110:check-access-host
# TT3: keep every script/control/mutation; recursive make shares the jobserver.
check-access-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --plain tools/tests/test_caller_access.py \
	    --control tools/tests/test_host32.py \
	    --host32 tools/tests/test_access_walk.py \
	    --host32 tools/tests/test_caller_copy.py \
	    --host32 tools/tests/test_db_caller.py \
	    --host32 tools/tests/test_surface_query.py

.PHONY: check-access-host
