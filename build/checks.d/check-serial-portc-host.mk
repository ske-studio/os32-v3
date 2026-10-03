CHECK_PAR_ORDER += 049:check-serial-portc-host
# 8255 ポート C (0035h) を丸ごと書かないこと (drivers/serial.c)。実機
# PC-9821Ra266 で rshell 中にビープが鳴り続けた件 (2026-09-24、
# docs/POLICY_DEBUG.md §4-59)。0035h は RS-232C の割り込み許可 bit0-2 と
# BUZ (bit3、0 = 鳴動)・SHUT0/SHUT1 などが同じバイトにいるので、許可は
# 0037h の BSR で 1 ビットずつ書く。BUZ の極性は UNDOCUMENTED (07h = 停止)。
# 偽の outp で 8255 を模型にし、初期化と ISR が 0035h を書かないこと・
# 上位 5 ビットが保たれること・ISR が RXRE を落として戻すこと (汲み残し時に
# IRQ4 の立ち上がりが出ること)・初期化が 1 のビットだけ落とすこと・V･FAST
# 経路・kernel/sys.c の buz_on/buz_off が書く値 (06h/07h) を見る。
# --mutate は写しの上で変異させるので並列 (check-par) で回せる。
check-serial-portc-host:
	python3 -B tools/tests/test_serial_portc.py $(MUT)

.PHONY: check-serial-portc-host
