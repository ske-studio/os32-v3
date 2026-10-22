CHECK_PAR_ORDER += 0202:check-docs-tree-host
# 合成の文書木と検査器の写しを使うので並列実行可能。
check-docs-tree-host:
	python3 -B tools/tests/test_doc_graph.py $(MUT)
	python3 -B tools/tests/test_doc_delink_archive.py $(MUT)

.PHONY: check-docs-tree-host
