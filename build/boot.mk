# ============================================================================
#  boot.mk — ブートローダーのビルドルール
# ============================================================================

# === スタンドアロン ASM (flat binary) ===
ASM_STANDALONE = boot/boot_fat.asm boot/loader_fat.asm boot/loader_fat_new.asm boot/boot_hdd.asm boot/loader_hdd.asm
BIN_STANDALONE = $(ASM_STANDALONE:.asm=.bin)

# ブート情報域 (0x7E00) の NASM 側。2 本のローダが %include するので、
# 変えたら両方を組み直す (票 TASK_HDD_INSTALL 段 0)。
BOOTINFO_INC = boot/bootinfo.inc boot/bootinfo_rm.inc
boot/loader_fat_new.bin boot/loader_fat144.bin boot/loader_hdd.o: $(BOOTINFO_INC)

# === 新HDDローダー (ASM + C リンク) ===
CFLAGS_BOOT = -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie \
              -fno-stack-protector -nostdlib -mno-red-zone -Os -Wall -fcommon \
              -Iboot

BOOT_C_SRC = boot/boot_main.c boot/ext2_mini.c boot/lz4_mini.c boot/vk32_boot.c
# 区画表の共有部 (drivers/pc98pt.c) を**写さずに**ローダにも組む
# (票 TASK_HDD_INSTALL 段 1-4)。カーネル・シェル・nhd_deploy.py と同じ配置で読む。
BOOT_PT_OBJ = boot/pc98pt_boot.o
BOOT_C_OBJ = $(BOOT_C_SRC:.c=.o) $(BOOT_PT_OBJ)
BOOT_ASM_OBJ = boot/loader_hdd.o
BOOT_ALL_OBJ = $(BOOT_ASM_OBJ) $(BOOT_C_OBJ)

boot/loader_hdd.o: boot/loader_hdd.asm
	$(AS) -f elf32 -o $@ $<

boot/boot_main.o: boot/boot_main.c boot/boot_defs.h drivers/pc98pt.h
	$(CC) $(CFLAGS_BOOT) -Idrivers -c -o $@ $<

$(BOOT_PT_OBJ): drivers/pc98pt.c drivers/pc98pt.h
	$(CC) $(CFLAGS_BOOT) -Idrivers -c -o $@ $<

boot/ext2_mini.o: boot/ext2_mini.c boot/boot_defs.h
	$(CC) $(CFLAGS_BOOT) -c -o $@ $<

boot/lz4_mini.o: boot/lz4_mini.c boot/boot_defs.h
	$(CC) $(CFLAGS_BOOT) -c -o $@ $<

# VK32 v2 の検査と展開 (票 TASK_SERIAL_HOSTFS A-4)。CRC32 の核は
# lib/crc32_core.inc (カーネル・hsync と同じ) を #include する。
boot/vk32_boot.o: boot/vk32_boot.c boot/boot_defs.h lib/crc32_core.inc
	$(CC) $(CFLAGS_BOOT) -c -o $@ $<

boot/loader_hdd.elf: $(BOOT_ALL_OBJ)
	$(LD) -m elf_i386 -T boot/loader.ld -o $@ $^ \
		-L$(shell $(CC) -print-libgcc-file-name | xargs dirname) -lgcc

boot/loader_hdd.bin: boot/loader_hdd.elf
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -gt 8192 ]; then \
		echo "ERROR: loader_hdd.bin is $$SIZE bytes (max 8192)"; \
		exit 1; \
	else \
		echo "loader_hdd.bin: $$SIZE / 8192 bytes"; \
	fi

# === 1.44MB (2HD 512B/sector) 版 ===
# 同じソースを `-DFD144` で組み直す。**別ファイルに写さない** — 写すと
# ジオメトリ定数が片方だけ直されて静かにずれる。
# 値の正典は tools/mkfat12.py の GEOMETRIES。
# boot_fat144.bin は 512B ちょうど (nasm の times が溢れを止める)。
BIN_FD144 = boot/boot_fat144.bin boot/loader_fat144.bin

boot/boot_fat144.bin: boot/boot_fat.asm
	$(AS) -f bin -DFD144 $< -o $@

boot/loader_fat144.bin: boot/loader_fat_new.asm
	$(AS) -f bin -DFD144 $< -o $@

# === 統合ターゲット ===
boot: $(BIN_STANDALONE) $(BIN_FD144) boot/loader_hdd.bin

# === クリーン ===
clean-boot:
	rm -f $(BIN_STANDALONE) $(BIN_FD144) $(BOOT_ALL_OBJ) boot/loader_hdd.elf boot/loader_hdd.bin

# === デバッグ版HDDローダー (ext2読み出しステップ確認用) ===
BOOT_DBG_C_SRC = boot/boot_debug.c
BOOT_DBG_C_OBJ = $(BOOT_DBG_C_SRC:.c=.o)
BOOT_DBG_ALL_OBJ = $(BOOT_ASM_OBJ) $(BOOT_DBG_C_OBJ) $(BOOT_PT_OBJ)

boot/boot_debug.o: boot/boot_debug.c boot/boot_defs.h drivers/pc98pt.h
	$(CC) $(CFLAGS_BOOT) -Idrivers -c -o $@ $<

boot/loader_hdd_debug.elf: $(BOOT_DBG_ALL_OBJ)
	$(LD) -m elf_i386 -T boot/loader.ld -o $@ $^ \
		-L$(shell $(CC) -print-libgcc-file-name | xargs dirname) -lgcc

boot/loader_hdd_debug.bin: boot/loader_hdd_debug.elf
	$(OBJCOPY) -O binary $< $@
	@SIZE=$$(wc -c < $@); \
	if [ $$SIZE -gt 8192 ]; then \
		echo "ERROR: loader_hdd_debug.bin is $$SIZE bytes (max 8192)"; \
		exit 1; \
	else \
		echo "loader_hdd_debug.bin: $$SIZE / 8192 bytes"; \
	fi

boot-debug: boot/loader_hdd_debug.bin

clean-boot-debug:
	rm -f boot/boot_debug.o boot/loader_hdd_debug.elf boot/loader_hdd_debug.bin

.PHONY: boot clean-boot boot-debug clean-boot-debug
