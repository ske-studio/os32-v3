CHECK_PAR_ORDER += 021:check-tests-inventory
# 登録列・recipe・試験スクリプト・入力対応表を照合する (生成文書は不要)。
check-tests-inventory:
	@python3 tools/gen_tests_inventory.py --check

.PHONY: check-tests-inventory
