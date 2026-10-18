# Only the new T5-0b size mutation; the normal controls always run first.
CHECK_PAR_ORDER += 144:check-sqlite-stack-host
check-sqlite-stack-host: sqlite-module-proto
	python3 -B tools/tests/test_sqlite_module_proto.py --directory "$(SQLITE_PROTO_OUT)" --vmkernel "$(BUILD_OUT)/vmkernel.lz4" $(if $(filter 1,$(MUTATE)),--stack-mutate)
.PHONY: check-sqlite-stack-host
