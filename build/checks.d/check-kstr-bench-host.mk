CHECK_PAR_ORDER += 099:check-kstr-bench-host
# kstring の実測プログラム kstr_bench の**計測の枠組み** (票
# docs/archive/portability/TASK_KSTRING_BENCH.md)。実物の
# userland/tests/kstr_bench.c を 1 行も写さず #include し、KernelAPI
# (get_tick / sys_write / sys_yield) と測られる 13 本だけを贋物にして回す。
# 見るのは数字ではなく**数字の作り方**: 出力の固定書式、1 ケース 1MB 以上 →
# 30 ティック未満なら倍 (倍は 5 回まで = 時計が止まっても終わる)、食い違いを
# 注入したら MISMATCH が出てその関数の計測が飛ぶこと、13 本が 4 者 (.asm の
# global / 表 / 改名表 / 贋物) で一致すること、[V2] の登録。
# 最後に**実物の .asm と .c を同居させた版**も回して 13 本の一致を見る (受入 K1)。
# --mutate は否定側 (倍にしない版 / 欄を入れ替えた版 / 飛ばさない版 など)。
# 記録は tools/tests/kstr_bench_tdd.md。
check-kstr-bench-host:
	python3 -B tools/tests/test_kstr_bench.py --target $(MUT)

.PHONY: check-kstr-bench-host
