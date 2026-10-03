CHECK_PAR_ORDER += 009:check-kbd-status-host
# キーボード 8251 のステータス判定 (drivers/kbd_status.c)。実機 PC-9821Ra266 で
# 本体キーボードの打鍵が一切届かなかった件 (docs/POLICY_DEBUG.md §4-57)。
# IRQ1 ハンドラは 0041h を読む前に 0043h を見て、RxRDY = 0 なら空 IRQ、
# PE/FE なら読み捨て + ER で解除、OE だけなら使って ER で解除、どれでもなければ使う。
# V86 へ IRQ1 を反射するのは実データのときだけ (kernel/v86_kbd.c の空読みは
# 前回のバイト) — 空 IRQ の反射でゲストに偽の ESC が届いていた。
# **EMPTY と ERROR は NP21/W では踏めない** (keyboard_i43 は `status | 0x85`
# を返し、IRQ1 の前に必ず RxRDY を立てる)。逆に `| 0x85` のビット
# (DSR / TxEMP / TxRDY) で DATA にならないとエミュレータの打鍵が全部落ちる。
# --target はカーネルと同じ i386-elf で drivers/kbd.c ごと通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/kbd_status_tdd.md。
check-kbd-status-host:
	python3 -B tools/tests/test_kbd_status.py --target $(MUT)

.PHONY: check-kbd-status-host
