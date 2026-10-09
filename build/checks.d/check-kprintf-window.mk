CHECK_PAR_ORDER += 030:check-kprintf-window
# X-15: CPL3 kprintf の可変引数が syscall のコピー窓に収まること。
check-kprintf-window:
	python3 -B tools/tests/test_kprintf_window.py $(MUT)

.PHONY: check-kprintf-window
