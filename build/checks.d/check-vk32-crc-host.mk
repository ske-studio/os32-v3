CHECK_PAR_ORDER += 007:check-vk32-crc-host
# VK32 v2 (CRC32 表 + 完全長) の生成と両ローダの検査。票 TASK_SERIAL_HOSTFS 部品 A-4。
# tools/mkvmkernel.py の生成物を zlib.crc32 と突き合わせ、HDD ローダの
# boot/vk32_boot.c (+ lz4_mini.c) と FD ローダの pm_vk32_boot (**ASM を切り出して
# 32bit で実行**) に同じ壊れたイメージ (1 ビット反転・切り詰め・entry_count 0・
# data_offset の範囲外・raw_size の不一致・展開先の範囲外・重なり・壊れた LZ4 列) を
# 渡し、同じ VK32_ERR_* で断ること、展開前に止まるものは窓を 1 バイトも書かないこと
# を見る。FD ローダの FAT チェーン検査 (fat_chain_check、32 ビットの番地で書いた
# 実モードの手続き) も bits 32 で組んで 2HD / 1.44MB の両方で回す。
# --real は make all の成果物 (build/out と images/ の 2HD / 1.44MB) が要る。
# --mutate は写しの上で変異 (組めない変異は ERROR、何も変えない対照を含む)。
# 記録は tools/tests/vk32_crc_tdd.md。
check-vk32-crc-host:
	python3 -B tools/tests/test_vk32_crc.py --real --target $(MUT)

.PHONY: check-vk32-crc-host
