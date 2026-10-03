CHECK_PAR_ORDER += 050:check-cpu-calibrate-host
# CPU 校正の止め方と丸め (kernel/cpu_calibrate_math.c)。実機 PC-9821Ra266 の
# シリアルが 9600 でも 38400 でも 1 バイト約 2ms しか出なかった件の真因
# (票 TASK_SERIAL_VFAST 往復 3)。校正ループを 1 周だけ回して tick で割って
# いたので、266MHz では 1 tick にも届かず elapsed 0 → 1 に丸められ、
# loops_per_tick が **実際の 1/7〜1/13** になっていた。その結果
# cpu_delay_us(5) が 0.5µs しか待たず、送信ループが TxRDY を待てずに
# hlt へ落ちていた。
# **NP21/W では踏めない** — 十分に遅いので 1 周で 5 tick を超える。
# 見るのは 3 つ: 速い CPU で 5 tick に届くまで回すこと、遅い CPU (8MHz) が
# 1 周のままであること (退行防止)、PIT が死んでも戻ってくること。
# --target はカーネルと同じ i386-elf で kernel/cpu_calibrate.c ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/cpu_calibrate_tdd.md。
check-cpu-calibrate-host:
	python3 -B tools/tests/test_cpu_calibrate.py --target $(MUT)

.PHONY: check-cpu-calibrate-host
