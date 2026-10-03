CHECK_PAR_ORDER += 003:check-bootinfo-host
# ブート情報域 0x7E00 (kernel/bootinfo_check.c + boot/bootinfo.inc)。票 TASK_HDD_INSTALL 段 0。
# ローダが INT 1Bh AH=84h の結果を書き、kernel_main が最初に写す。見るのは
# magic / 反転チェック語 (書きかけ) / 版 / ドライブ記録の和 / BX=512・CX/DH/DL≠0・
# CF=0・問い合わせ済み・ローダの valid の規則と、[hdd] 行の書式、
# **NASM 側の写し boot/bootinfo.inc の値が include/bootinfo.h と名前ごとに一致**すること。
# --target は i386-elf で bootinfo.c (構造体の並びの STATIC_ASSERT) と ide.c を通す。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/bootinfo_tdd.md。
check-bootinfo-host:
	python3 -B tools/tests/test_bootinfo.py --target $(MUT)

.PHONY: check-bootinfo-host
