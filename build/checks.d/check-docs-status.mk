CHECK_PAR_ORDER += 019:check-docs-status
# 票の状態行の語彙 (docs/tasks/**/*.md の冒頭の `状態:`)。語彙は docs/POLICY_DEV.md §8 の
# 表から読む (道具は語を持たない)。2026-09-29 の v2.1 の棚卸しで語彙の外の状態行が 12 本、
# 実態と食い違う状態行が 27 本あったので足した。docs/archive/ は見ない (本文を書き換えない規則)。
check-docs-status:
	@python3 tools/check_docs_status.py

.PHONY: check-docs-status
