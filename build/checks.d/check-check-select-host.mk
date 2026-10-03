CHECK_PAR_ORDER += 094:check-check-select-host
# check-changed の選び方の試験 (代行レビュー P2-1〜P2-3 の筋書き: .inc の取り込み、
# 裸の文書名、docs だけの変更で文書の検査を常に回す、走査型 glob の保険、
# feat/gui の上でコミットした後の基点。Makefile / build/*.mk の「新しい試験を足す形」
# の型の一致と、独立レビューの反例が全部に倒れること)。--mutate は check_select.py の
# 写しに当てるので並列 (check-par) で回せる。
check-check-select-host:
	python3 -B tools/tests/test_check_select.py $(MUT)

	python3 -B tools/tests/test_checkinfra.py $(MUT)

.PHONY: check-check-select-host
