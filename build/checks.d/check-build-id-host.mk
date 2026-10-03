CHECK_PAR_ORDER += 008:check-build-id-host
# カーネルに埋め込むコミット ID の生成器 (tools/gen_build_id.py)。一時の git
# リポジトリで clean / -dirty (未追跡は数えない) / unknown / 同じなら書かない を見る。
# 票 TASK_SERIAL_HOSTFS 部品 A-4 (ver の Commit)。記録は tools/tests/vk32_crc_tdd.md。
check-build-id-host:
	python3 -B tools/tests/test_build_id.py $(MUT)

.PHONY: check-build-id-host
