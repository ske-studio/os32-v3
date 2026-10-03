CHECK_PAR_ORDER += 089:check-time-math-host
# µs 時計の判定と算数 (kernel/time_math.c、票 TASK_HAL_WIRING §1-5)。
# 見るのは p1/p2 の判定表 3 分岐 (実 PIT では呼び出しから p1 読みまでに境界を
# 越える機械があり、位相待ちでは 0/0 と 0/1 を撃ち分けられない) と、
# **71 分の桁あふれ** (tick との積を u32 で組むと 429496 tick で時刻が 0 に
# 戻る。起動から 71 分はエミュレータでも実機でも 1 度も回していない)。
# --target は kernel/ktime.c ごと通す。記録は tools/tests/time_math_tdd.md。
check-time-math-host:
	python3 -B tools/tests/test_time_math.py --target $(MUT)

.PHONY: check-time-math-host
