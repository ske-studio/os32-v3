CHECK_PAR_ORDER += 059:check-con-sink-host
# kernel/con_sink.c のリング (票 K6C)。実物のソースをホスト ILP32 で走らせ、
# 同じソースが i386-elf-gcc -Werror でも通ることを見る。記録は
# tools/tests/con_sink_tdd.md。
check-con-sink-host:
	python3 -B tools/tests/test_con_sink.py

.PHONY: check-con-sink-host
