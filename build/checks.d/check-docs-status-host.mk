CHECK_PAR_ORDER += 020:check-docs-status-host
# 上の検査器のホスト試験。小さな木を作って判定を固定し、実物の木が通ることも見る。
# --mutate は検査器の写しに当てるので並列 (check-par) で回せる。記録は tools/tests/docs_status_tdd.md。
check-docs-status-host:
	python3 -B tools/tests/test_docs_status.py $(MUT)

.PHONY: check-docs-status-host
