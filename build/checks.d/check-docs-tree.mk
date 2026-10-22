CHECK_PAR_ORDER += 0201:check-docs-tree
# 現在の違反を基準値と比較。更新は明示的な --update-baseline だけ。
check-docs-tree:
	python3 -B tools/doc_graph.py

.PHONY: check-docs-tree
