CHECK_PAR_ORDER += 142:check-sqlite-module-proto-host
# 記録: tools/tests/sqlite_module_proto_tdd.md
check-sqlite-module-proto-host: sqlite-module-proto
	python3 -B tools/tests/test_sqlite_module_proto.py --directory "$(SQLITE_PROTO_OUT)" --vmkernel "$(BUILD_OUT)/vmkernel.lz4" $(MUT)

.PHONY: check-sqlite-module-proto-host
