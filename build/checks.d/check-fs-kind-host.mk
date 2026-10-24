CHECK_PAR_ORDER += 072:check-fs-kind-host
# TT4: preserve script arguments, controls and mutations; share the jobserver.
check-fs-kind-host:
	+python3 -B tools/run_host_group.py $(MUT) \
	    --command "tools/tests/test_fs_kind.py --target" \
	    --command "tools/tests/test_fs_kind_callers.py --target $(MUT)" \
	    --command "tools/tests/test_vfs_kind.py --target"

.PHONY: check-fs-kind-host
