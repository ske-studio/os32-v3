CHECK_PAR_ORDER += 010:check-kbd-dlog-host
# キーボードの受信記録 (drivers/kbd_dlog.c、KAPI v67 kbd_diag_log) と `kbdstat -w`
# の行 (userland/shell/kbd_watch.c)。票 docs/tasks/gui/TASK_KBD_NAV.md §3 — 実機で
# カナ / CAPS が「ロックで make、解除で break」か「押すたびに make だけ」かを見る準備。
# 32 件の循環 (seq の飛び = 取りこぼし)、IRQ1 は EMPTY / ERROR を積まず OVERRUN は
# 印付きで積む、修飾は配った後の値、kbd_diag_log の引数検査、行と LOST 行の書式。
# drivers/kbd.c は IRQ1 ハンドラごと実物を回す (0043h / 0041h は
# tools/tests/kbd_hostshim/io.h の模型)。**EMPTY / ERROR は NP21/W では踏めない**。
# --target はカーネルと同じ i386-elf で 3 本を通す (構造体の大きさの STATIC_ASSERT)。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/kbd_dlog_tdd.md。
check-kbd-dlog-host:
	python3 -B tools/tests/test_kbd_dlog.py --target $(MUT)

.PHONY: check-kbd-dlog-host
