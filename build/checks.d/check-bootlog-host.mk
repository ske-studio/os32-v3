CHECK_PAR_ORDER += 060:check-bootlog-host
# kernel/bootlog.c — 最後の起動のログ (/var/log/boot.log)。実物のソースをホストで
# 走らせ (溜める・あふれ・組み立て・書き出しの手順を偽の VFS で)、変異を当て、
# 同じソースが i386-elf-gcc -Werror でも通ることを見る。記録は
# tools/tests/bootlog_tdd.md。
check-bootlog-host:
	python3 -B tools/tests/test_bootlog.py --target $(MUT)

.PHONY: check-bootlog-host
