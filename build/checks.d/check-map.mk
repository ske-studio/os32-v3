CHECK_PAR_ORDER += 093:check-map
# 対応表 tools/check_map.yaml の検査 (列との過不足・古い glob・入力の漏れ)。
check-map:
	@python3 tools/check_select.py --lint

.PHONY: check-map
