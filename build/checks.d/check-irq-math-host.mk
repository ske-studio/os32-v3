CHECK_PAR_ORDER += 088:check-irq-math-host
# 動的 IRQ の判断 (kernel/irq_math.c、票 TASK_HAL_WIRING §1-1)。
# 見るのは **NP21/W でも実機でも狙って作れない**重なり: A と B が同時に要因を
# 持つ (最初の HANDLED で打ち切らない)、1 巡目で受けて 2 巡目が空
# (handled_any を 0 で上書きしない)、1 tick に 200 回と 201 回 (ストームの
# 閾値)、IRQ15 のスレーブ ISR bit7 が落ちている (スプリアスにスレーブ EOI を
# 送らない)、登録の拒否規則 (固定 IRQ / 範囲外 / 重複 / SHARED 不一致 /
# 5 件目 / 隔離済み / ISR 文脈)。
# --target はカーネルと同じ i386-elf で kernel/irq.c ごと通す。
# 記録は tools/tests/irq_math_tdd.md。
check-irq-math-host:
	python3 -B tools/tests/test_irq_math.py --target $(MUT)

.PHONY: check-irq-math-host
