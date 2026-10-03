CHECK_PAR_ORDER += 021:check-tests-inventory
# 試験一覧 docs/TESTS.md の鮮度検査 (文書整理 段階 E)。表は build/*.mk と試験
# スクリプトから tools/gen_tests_inventory.py が生成するので、ターゲットを足した
# のに一覧が古いままという状態を止める。check_kapi_version.py と同じ「生成物と
# 正典の照合」の作法で、ずれたら --write を促して落ちる。手書きの節は
# docs/TESTS.md の `<!-- manual:… -->` 区間だけで、生成器はそこを読み戻して保つ。
check-tests-inventory:
	@python3 tools/gen_tests_inventory.py --check

.PHONY: check-tests-inventory
