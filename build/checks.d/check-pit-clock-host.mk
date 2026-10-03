CHECK_PAR_ORDER += 051:check-pit-clock-host
# PIT の分周 (kernel/pit_math.c)。pit_init が 1.9968MHz 決め打ちで割っていて、
# 2.4576MHz 系 (実機 PC-9821Ra266、0000:0501h bit7 = 0) では 100Hz のつもりの
# tick が 123Hz = 8.125ms になっていた件 (票 TASK_HAL_WIRING §1-0)。
# **NP21/W は 1.9968MHz 設定なのでエミュレータでは一度も踏めない**
# (§4-49・§4-51・§4-53 と同じ型)。
# 見るのは 4 つ: 両クロックのリロード値 (19968 / 24576、周期はどちらも 10000µs)、
# 100Hz 以外を**既定へ倒さずに**断ること、未知のクロックで割らないこと、
# period_us の中間積が 32bit で溢れないこと (倍率を `U` で持って、64bit ホスト
# でも溢れが再現するようにしてある)。
# --target はカーネルと同じ i386-elf で kernel/sysclk.c と kernel/idt.c ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/pit_clock_tdd.md。
check-pit-clock-host:
	python3 -B tools/tests/test_pit_clock.py --target $(MUT)

.PHONY: check-pit-clock-host
