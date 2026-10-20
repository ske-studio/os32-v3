CHECK_PAR_ORDER += 193:check-kpeek-host
# kdebug の peek 本体 (kdebug/kpeek.c・kpeek_allow.c、TASK_KPEEK §2-4・§2-5)。
# 実物のソースを ILP32 で回し (import と読みは贋物)、i386-elf -Werror でも通す。
check-kpeek-host:
	python3 -B tools/tests/test_kpeek.py $(MUT)
