# x86 kernel products; source selection belongs to build/select.mk.

# === コミット ID (票 TASK_SERIAL_HOSTFS A-4) ===
# 生成器は毎回走るが、中身が変わったときだけ build_id.c を書き直す。
# コミット ID が変わっても組み直すのは build_id.o 1 つとリンクだけ。
BUILD_ID_SRC = $(BUILD_OUT)/build_id.c
BUILD_ID_OBJ = $(BUILD_OUT)/build_id.o

$(BUILD_ID_SRC): .FORCE
	@mkdir -p $(BUILD_OUT)
	@python3 tools/gen_build_id.py -o $@

$(BUILD_ID_OBJ): $(BUILD_ID_SRC)
	$(CC) $(CFLAGS_BASE) -Iinclude -c $< -o $@

# kapi/ モジュール
KAPI_GENERATED := kapi/kapi_generated.c exec/exec_kapi_init.inc \
    sdk/include/os32/os32_kapi_generated.h sdk/include/os32/os32_kapi_slots.h \
    sdk/include/os32/os32_generations.h sdk/include/os32/os32_unit_stamp.h \
    sdk/os32_generations.py sdk/rust/os32api/src/generations.rs \
    sdk/crt/generations.inc sdk/link/generations.ld \
    sdk/link/app.ld sdk/link/app_sys.ld sdk/link/shlib.ld build/os32.ld
$(KAPI_GENERATED) &: sdk/kapi.json sdk/gen_kapi.py sdk/kapi_abi_x86.py sdk/target_profile.py
	python3 sdk/gen_kapi.py

# GCC honors SOURCE_DATE_EPOCH for __DATE__/__TIME__ in kapi_sys.c.
# Without that environment variable the normal wall-clock build date remains.
.FORCE:

$(BUILD_OUT)/kernel.elf: $(BUILD_OUT)/os32.ld $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ)
	$(LD) $(LDFLAGS) -o $@ $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ)

$(BUILD_OUT)/kernel.bin: $(BUILD_OUT)/kernel.elf
	$(OBJCOPY) -O binary $< $@

# VK32 now contains only the resident core; SQLite is a paired /boot module.
$(BUILD_OUT)/vmkernel.lz4: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/kernel.elf tools/mkvmkernel.py tools/gen_memmap.py include/memmap.h boot/boot_defs.h
	python3 tools/mkvmkernel.py --layout-header $(LAYOUT_HEADER) \
		--elf $(BUILD_OUT)/kernel.elf --kernel $(BUILD_OUT)/kernel.bin -o $@

# === カーネル単独ターゲット ===
# ELF → bin → VK32 → 世代 → 配置の順。配備だけの再リンクでも照合表を更新する。
$(BUILD_OUT)/generations-manifest.json: $(BUILD_OUT)/vmkernel.lz4 tools/gen_generation_manifest.py sdk/kapi.json .FORCE
	python3 tools/gen_generation_manifest.py

$(BUILD_OUT)/layout-manifest.json: $(BUILD_OUT)/generations-manifest.json tools/gen_memmap.py $(LAYOUT_HEADER)
	python3 tools/gen_memmap.py --manifest --write

# カーネルを運ぶ経路は、再リンク後の世代表・配置表まで確定してから配備する。
# 生成規則と一緒に結線し、deploy.mk 単独の利用者には生成済み成果物を渡せる。
deploy deploy-u3 deploy-kernel deploy-nhd nhd-migrate-pt deploy-fd: $(BUILD_OUT)/layout-manifest.json

# 像の CRC (pair) を埋める部品の一覧。T4c-1 で SQLite、T5 で起動時の部品を足す。
KERNEL_PAIRED_MODULES := $(BUILD_OUT)/kdebug.bin $(BUILD_OUT)/sqlite.mod $(BUILD_OUT)/fep.mod $(BUILD_OUT)/cd.mod $(BUILD_OUT)/kcg.mod $(BUILD_OUT)/fm.mod $(BUILD_OUT)/pcm.mod $(BUILD_OUT)/v86.mod $(BUILD_OUT)/lan.mod $(BUILD_OUT)/cirrus.mod $(BUILD_OUT)/hwdev.mod $(BUILD_OUT)/geforce.mod $(BUILD_OUT)/trident.mod
kernel: $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/layout-manifest.json $(KERNEL_PAIRED_MODULES)

# === カーネルクリーン ===
clean-kernel:
	rm -f $(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(ASM_KERNEL:.asm=.o) $(C_KERNEL:.c=.o) arch/x86/kernel/ring3_ls_shim.o $(BUILD_ID_SRC) $(BUILD_ID_OBJ) $(BUILD_OUT)/kernel.elf $(BUILD_OUT)/kernel.bin $(BUILD_OUT)/vmkernel.lz4 $(BUILD_OUT)/kernel.map

.PHONY: kernel clean-kernel

# LAN (LGY-98) 有効カーネル。emu_agent の許可リストから呼べる 1 語ターゲット。
# スタンプ (build/config.mk LGY98_STAMP) を置いてからビルドするので、以後の
# make kernel / deploy-nhd も LAN 有効のまま。kernel-nolgy98 で無効化する (stamp 無しの既定は有効)。
kernel-lgy98:
	@mkdir -p $(BUILD_OUT)
	@echo 5 > $(LGY98_STAMP)
	$(MAKE) kernel

# リンク層 L0 試験用 (DIAG 1 + LINKTEST 8 = 9、反射なし)。Host Agent
# (tools/host_agent.py) を先に起動しておくこと。
kernel-lgy98-link:
	@mkdir -p $(BUILD_OUT)
	@echo 9 > $(LGY98_STAMP)
	$(MAKE) kernel

kernel-nolgy98:
	@mkdir -p $(BUILD_OUT)
	@echo off > $(LGY98_STAMP)
	$(MAKE) kernel

.PHONY: kernel-lgy98 kernel-lgy98-link kernel-nolgy98

# M2 の対向試験 (inject → 受信 → 反射送信 → capture)。NP21/W が LAN 有効 + 反射モードの
# カーネルで起動していること。make check には入れない (実機が要る)。
check-net-m2:
	@python3 tools/net_m2_test.py

# M3: CPL3 プログラム常駐中に同じ試験を回す。aidebug のデバッグサーバは単一スレッドで
# 要求を 1 つずつ処理するため、戻ってこないコマンド (sleep 等) を /api/cmd で投げると
# inject / capture が詰まって試験にならない。less のような対話プログラムなら最初の画面で
# キー待ちに入って /api/cmd が戻り (チャネルが空く)、プログラムは CPL3 に常駐したままになる。
# → NIC IRQ がアプリの CR3 が有効な状態で配送・処理されることを確認する。
check-net-m2-cpl3:
	@python3 tools/net_m2_test.py --during-cmd "less /etc/profile" --wait-for "/etc/profile" --exit-key q --during-timeout 30

# M4: フロー制御なしでリングを故意に飽和させ、ドライバが wedge せず 100Hz
# ウォッチドッグで自己回復することを確認する (溢れても壊れない安全網)。
check-net-m4:
	@python3 tools/net_m4_test.py

# L0: リンク層 (Stop-and-Wait) の HELLO + PING/PONG 10 往復を確認する。
# 先に tools/host_agent.py を NP2NETSOCK ポートで起動し、kernel-lgy98-link を配備する。
check-net-l0:
	@python3 tools/net_l0_test.py

# L1: WINDOW/Credit のフロー制御。ホストに 200 DATA を流させ、絶対値 WINDOW で
# リングを溢れさせずに全部・順序どおり受けられることを確認する (host_agent.py が対向)。
check-net-l1:
	@python3 tools/net_l1_test.py

# L2: ストリーミング。128KB を 8KB バッファで順次消費し、欠落を Go-Back-N で回復する。
check-net-l2:
	@python3 tools/net_l2_test.py

# L3: Host Services。ホストに GET (本文をストリーム) / TIME (RPC) をさせ、OS32 が
# 要求を出して結果だけ受け取ることを確認する。
check-net-l3:
	@python3 tools/net_l3_test.py

.PHONY: check-net-m2 check-net-m2-cpl3 check-net-m4 check-net-l0 check-net-l1 check-net-l2 check-net-l3

$(R1_OUT)/os32.ld: $(BUILD_OUT)/os32.raw.ld
	@mkdir -p $(@D)
	sed -E 's@([a-zA-Z0-9_/]+\.o)@$(R1_OBJ)/\1@g' $< > $@

$(R1_OUT)/kernel.elf: $(R1_OBJECTS) $(R1_OUT)/os32.ld
	$(LD) $(filter-out -Map=% -T $(BUILD_OUT)/os32.ld,$(LDFLAGS)) -T $(R1_OUT)/os32.ld -Map=$(R1_OUT)/kernel.map -o $@ $(R1_OBJECTS)

$(R1_OUT)/kernel.bin: $(R1_OUT)/kernel.elf
	$(OBJCOPY) -O binary $< $@

$(R1_OUT)/vmkernel.lz4: $(R1_OUT)/kernel.bin $(R1_OUT)/kernel.elf tools/mkvmkernel.py tools/gen_memmap.py include/memmap.h boot/boot_defs.h
	python3 tools/mkvmkernel.py --layout-header $(LAYOUT_HEADER) --elf $(R1_OUT)/kernel.elf --kernel $(R1_OUT)/kernel.bin -o $@

R1_PAIRED_MODULES := $(patsubst $(BUILD_OUT)/%,$(R1_OUT)/%,$(KERNEL_PAIRED_MODULES))
kernel-r1: $(R1_OUT)/vmkernel.lz4 $(R1_PAIRED_MODULES)
	python3 tools/r1_manifest.py $(R1_OUT)

-include $(R1_OBJECTS:.o=.d)
.PHONY: kernel-r1

# One generated header drives C, ld and NASM; FORCE re-runs the generator
# while write-if-changed keeps an unchanged build incremental.
$(LAYOUT_HEADER): include/memmap.h tools/gen_memmap.py .FORCE
	@python3 tools/gen_memmap.py --layout-header $@

$(BUILD_OUT)/os32.raw.ld: build/os32.ld $(LAYOUT_HEADER)
	$(LAYOUT_CPP) $< -o $@

$(BUILD_OUT)/os32.ld: $(BUILD_OUT)/os32.raw.ld build/config.mk build/kernel.mk
	sed -E 's@([a-zA-Z0-9_/]+\.o)@$(KERNEL_OBJ_PREFIX)\1@g' $< > $@

$(ASM_KERNEL_OBJ) $(C_KERNEL_OBJ) $(BUILD_ID_OBJ) $(R1_OBJECTS): $(LAYOUT_HEADER) build/kernel.mk build/config.mk

# Finish every paired image before snapshotting the boot distribution.
$(BUILD_OUT)/generations-manifest.json: $(KERNEL_PAIRED_MODULES)

# Raster diagnostics have their own object tree, map, image and paired modules.
# Normal make kernel/all never defines SPLASH_RASTER_DIAG.
.PHONY: kernel-splash-diag
kernel-splash-diag:
	$(MAKE) BUILD_OUT=$(BUILD_OUT)/splash-diag SPLASH_DIAG_BUILD=1 splash-diag-image

# The normal manifest generators describe build/out, not alternate images.
# Keep this diagnostic target confined to its image and matching modules.
.PHONY: splash-diag-image
splash-diag-image: $(BUILD_OUT)/vmkernel.lz4 $(KERNEL_PAIRED_MODULES)

# Observation has a separate type capacity, image, map and paired modules.
.PHONY: kernel-splash-obs splash-obs-image
kernel-splash-obs:
	$(MAKE) BUILD_OUT=$(BUILD_OUT)/splash-obs SPLASH_OBS_BUILD=1 splash-obs-image

splash-obs-image: $(BUILD_OUT)/vmkernel.lz4 $(KERNEL_PAIRED_MODULES)

# Match splash diagnostics: isolated objects/image and CRC-paired modules.
.PHONY: kernel-pcm-e4-diag pcm-e4-diag-image
kernel-pcm-e4-diag:
	$(MAKE) BUILD_OUT=$(BUILD_OUT)/pcm-e4-diag PCM_E4_DIAG_BUILD=1 pcm-e4-diag-image
pcm-e4-diag-image: $(BUILD_OUT)/vmkernel.lz4 $(KERNEL_PAIRED_MODULES)
