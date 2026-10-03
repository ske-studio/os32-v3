CHECK_PAR_ORDER += 025:check-p2v
# [C5] 物理ポインタ変換の走査と、写しへの既知の違反4形の注入。
check-p2v:
	python3 tools/check_p2v.py
	python3 tools/tests/test_p2v.py $(MUT)

.PHONY: check-p2v
