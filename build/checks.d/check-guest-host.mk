CHECK_PAR_ORDER += 108:check-guest-host
# ゲストで一括実行してホストで集計するランナー (票 docs/archive/test/
# TASK_TEST_RUNNER.md)。**2 つのターゲットは別物なので混ぜないこと。**
#
#   check-guest-host  ランナーの**ホスト試験** (受入 R7)。NP21/W に触らない。
#                     生成 (一覧 → 平らなスクリプト) と集計 (出力 → 判定) は
#                     エミュレータにも時計にも触らない純関数に切ってあるので、
#                     贋物の入力だけで全部踏める。**そこが壊れていたらゲストで
#                     回しても意味がない**ので、これは `check` の列に入れる
#                     (変異は tools/guest_tests.py の写しの木に当てる —
#                     tools/tests/mutpar.py、並列で回せる)。
#                     --mutate の否定側: 食い違い (0 なのに FAIL) の見逃し /
#                     見張りが発火しない / 固まった試験を名指ししない /
#                     /host が無いのに合格にする / 落ちた試験 (139) で後続を
#                     打ち切る / 前回の出力が混ざる (R8)。
#                     記録は tools/tests/guest_tests_tdd.md。
#
#   check-guest       **本番。ゲストで実際に走らせる。**`make check` の列には
#                     入れない — NP21/W が動いている必要があり ([D1] の領域)、
#                     `make check` はホストだけで完結する約束だから (票 §4)。
#                     走らせる一覧は tools/tests/guest_tests.txt。
check-guest-host:
	python3 -B tools/tests/test_guest_tests.py $(MUT)

.PHONY: check-guest-host
