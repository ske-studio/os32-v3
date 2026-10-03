CHECK_PAR_ORDER += 027:check-c-dialect-host
# check-c-dialect の検査器の試験。--mutate は否定側: 実物の木の写し
# (tools/tests/mutpar.py) に変異 (VLA・暗黙宣言・偽の _Static_assert・SDK ヘッダへの
# C11 構文ほか) を当てて検査器が落ちることを見る。記録は tools/tests/c_dialect_tdd.md。
check-c-dialect-host:
	python3 -B tools/tests/test_c_dialect.py $(MUT)

.PHONY: check-c-dialect-host
