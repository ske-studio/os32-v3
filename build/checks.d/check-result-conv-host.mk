CHECK_PAR_ORDER += 107:check-result-conv-host
# 試験プログラムの合否を機械が読める形にする約束事 (票 docs/archive/test/
# TASK_TEST_RESULT.md §2 / §11)。終了コード (0 / 1 / 2、予約値 126/127/130/139 は
# 返さない) と最終行の集計行 `<名前>: PASS <n>/<m>` が**必ず一致する**ことと、
# その集計行が **fd 1 に出る**ことを固定する。実物の userland/lib/rt/testresult.h
# と、userland/tests/ の実物のプログラム 5 本 (stat_t / restest / test2 /
# klibc_test / font_load_test) を贋物の KernelAPI で**実際に走らせ**、
# **どの fd に何が書かれたか**と main の返り値の両方を観測する — grep では
# 一致は確かめられないし、画面 (kprintf) と fd 1 を同じバッファへ流す贋物では
# 票 §11 の穴 (16 本中 14 本の集計行がリダイレクトで拾えなかった) を見逃す。
# 静的側は第 1 陣 (票 §4) 16 本の `void main` / 名前が argv[0] 由来 /
# main の return が集計の答えでない / ヘッダの内部 (os32_test__*) を直接呼ぶ、
# を見る。Rust の alloc_demo は書式と終了コードを testresult.h と突き合わせる。
# --mutate は否定側 (どれもコンパイルは通る変異)。
# 記録は tools/tests/result_conv_tdd.md。
check-result-conv-host:
	python3 -B tools/tests/test_result_conv.py --target $(MUT)

.PHONY: check-result-conv-host
