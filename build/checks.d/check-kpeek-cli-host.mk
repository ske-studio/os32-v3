CHECK_PAR_ORDER += 193:check-kpeek-cli-host
# shell の `peek` (userland/shell/cmd_peek.c、TASK_KPEEK 段 T3)。実物を
# tools/tests/kpeek_cli_host.c が #include し、KernelAPI の kprintf /
# kdebug_call / version だけを偽物にする。見るのは引数 (-k/-p/-m、-w、16 進、
# -m は -w 必須)、64 KB 上限・幅の倍数・4 GB 越えを呼ばずに断ること、1 KB の
# 分割、行の書式と 8 bit 和、最後の端数行、CRC32 と frames、誤りは 1 行だけ、
# NOSYS の案内。--mutate は写しの上で変異させるので並列で回せる。
check-kpeek-cli-host:
	python3 -B tools/tests/test_kpeek_cli.py $(MUT)

.PHONY: check-kpeek-cli-host
