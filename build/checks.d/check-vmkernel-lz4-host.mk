CHECK_PAR_ORDER += 006:check-vmkernel-lz4-host
# vmkernel.lz4 の LZ4 高圧縮 (tools/mkvmkernel.py、HC level 12) と展開側 3 実装。
# 票 TASK_SERIAL_HOSTFS 部品 A-1 / TASK_HDD_INSTALL N8 (生成側の上限検査)。
# HDD ローダの boot/lz4_mini.c、lz4 コマンドの lib/lz4.c、FD ローダの
# boot/loader_fat_new.asm の pm_lz4_decode (**ASM を切り出して 32bit で実行**) が、
# build/out の実物と合成データ (延長 255 跨ぎ・offset >= 32768・重なり) を
# バイト一致で展開すること、合計が MAX_IMAGE_SIZE を超えたら生成が失敗すること
# (ちょうどは通り +1 は落ちる) を見る。--real は make all の成果物が要る。
# --target はデコーダをローダと同じ i386-elf-gcc で組む。
# --mutate は写しの上で変異するので並列 (check-par) で回せる。
# 記録は tools/tests/vmkernel_lz4_tdd.md。
check-vmkernel-lz4-host:
	python3 -B tools/tests/test_vmkernel_lz4.py --real --target $(MUT)

.PHONY: check-vmkernel-lz4-host
