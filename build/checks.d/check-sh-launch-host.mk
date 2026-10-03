CHECK_PAR_ORDER += 065:check-sh-launch-host
# userland/shell/sh_launch.inc の起動待ち (票 T9 D3a)。実物のソースを
# tools/tests/sh_launch_host.c がそのまま #include し、KernelAPI の
# launch_req / launch_poll / sys_yield / kprintf を差し替えて DONE / FAILED /
# STALE / FULL の 4 経路と「待ちの間 kbd_* / ime_* を呼ばない」を見る。
# 同じソースが i386-elf-gcc -Werror でも通ることも別に見る。記録は t9_tdd.md。
check-sh-launch-host:
	python3 -B tools/tests/test_sh_launch.py

.PHONY: check-sh-launch-host
