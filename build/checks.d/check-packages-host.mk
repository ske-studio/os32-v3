CHECK_PAR_ORDER += 023:check-packages-host
# CD のパッケージが配備マニフェストと一致しているか (tools/mkpkg.py --plan、
# 構成は build/packages.yaml)。配備の各ファイルがちょうど 1 つのパッケージに
# 入っている (または理由つきで除外されている) こと、userland/tests/ 由来が
# test タグであること、128 項目を超えたら分割されること、実物の mkpkg で作った
# PKG と ISO の中の PKG を読み戻して配備の全ファイルが同じバイト列で揃うこと、
# cdinst.c のベース名が構成と一致することを見る。成果物を読むので make all の後。
check-packages-host:
	python3 -B tools/tests/test_packages.py

.PHONY: check-packages-host
