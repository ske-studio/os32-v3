CHECK_PAR_ORDER += 061:check-kbd-inject-host
# kernel/kbd_inject.c の 256B リング (票 K7)。実物のソースを kernel/con_sink.c と
# 同じ翻訳単位で走らせ (注入の権限は con_sink の読み手 1 本)、同じソースが
# i386-elf-gcc -Werror でも通ることを見る。記録は tools/tests/k7_tdd.md。
check-kbd-inject-host:
	python3 -B tools/tests/test_kbd_inject.py $(MUT)

.PHONY: check-kbd-inject-host
