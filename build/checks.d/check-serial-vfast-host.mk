CHECK_PAR_ORDER += 048:check-serial-vfast-host
# シリアルの速度判定 (drivers/serial_plan.c)。実機 PC-9821Ra266 との会話が
# シリアルしかなく、9600 で 490B/s しか出ていなかった件 (票 TASK_SERIAL_VFAST)。
# **NP21/W は通信速度を模擬しない**ので、ここはエミュレータでは踏めない。
# 見るのは 4 つ: V･FAST の速度→分周表 (013Ah bit3-0)、8253 の整数分周
# (1.9968MHz の 38400 は 41600 に化ける / 2.4576MHz なら count 4)、
# TxRDY を待つ予算 (2 × 10 ビット ÷ baud — 0 にすると直す前の hlt 待ちに戻る)、
# **互換 0032h と FIFO 0132h のビット位置の違い** (RxRDY が bit1 と bit2、
# 0x04 は互換では TxEMP なので取り違えても落ちずに静かに壊れる)。
# --target はカーネルと同じ i386-elf で drivers/serial.c ごと通す。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
# 記録は tools/tests/serial_vfast_tdd.md。
check-serial-vfast-host:
	python3 -B tools/tests/test_serial_vfast.py --target $(MUT)

.PHONY: check-serial-vfast-host
