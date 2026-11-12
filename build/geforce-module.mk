# T5c-5b: reuse T4 function imports, VA0 packing and paired distribution.
GEFORCE_OUT := $(BUILD_OUT)/geforce
GEFORCE_GEN := $(addprefix $(GEFORCE_OUT)/,geforce_imports.h geforce_imports.json geforce_import_slots.c geforce_import_stubs.S geforce_provider_types.c geforce_kexports.c)
GEFORCE_DEF_SOURCES := gfx/gfx_policy.h gfx/gfx_osri_view.h include/debug_probe.h hwdev/gf/command.h hwdev/gf/gf1_test.h hwdev/gf/gf2_test.h kdebug/gfinit_bus.h arch/x86/arch_io.h drivers/pci/pci.h drivers/video/geforce/geforce_module.h drivers/video/geforce/geforce_tick.h drivers/video/pc98/pc98_route.h gfx/palette.h hwdev/gf/foundation.h hwdev/gf/internal.h hwdev/gf/production.h include/gfx_hal.h include/gfx_hal2.h include/gfx_mode.h include/io.h include/kprintf.h include/sys.h include/video_service.h kernel/irq/irq.h kernel/irq/tick.h lib/kstring.h lib/serial_diag.h mm/paging.h mm/pgalloc.h
$(GEFORCE_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/module_schema.py tools/prototypes/geforce_imports.def $(GEFORCE_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module geforce --out-dir $(GEFORCE_OUT)

GEFORCE_OBJS := $(addprefix $(GEFORCE_OUT)/,geforce_body.o backend_geforce.o gf_present.o gf_hook.o gf_direct.o gf_present_m2mf.o $(notdir $(patsubst %.c,%.o,$(wildcard drivers/video/geforce/gf_v2_*.c))) geforce_import_slots.o geforce_import_stubs.o)
GEFORCE_INC := $(INC_KERNEL) -I. -I$(GEFORCE_OUT)
$(GEFORCE_OUT)/geforce_body.o: drivers/video/geforce/geforce_body.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/backend_geforce.o: drivers/video/geforce/backend_geforce.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/gf_present.o: drivers/video/geforce/gf_present.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
$(patsubst drivers/video/geforce/%.c,$(GEFORCE_OUT)/%.o,$(wildcard drivers/video/geforce/gf_v2_*.c)) $(GEFORCE_OUT)/gf_hook.o $(GEFORCE_OUT)/gf_direct.o $(GEFORCE_OUT)/gf_present_m2mf.o: $(GEFORCE_OUT)/%.o: drivers/video/geforce/%.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GEFORCE_INC) -c $< -o $@
# P3 service and P2 runner are linked independently of diagnostic HWDEV/KDEBUG.
GEFORCE_LAYER2_SOURCES := $(addprefix hwdev/gf/,fifo.c push.c dmaobj.c ramht.c grctx.c gr.c fence.c fault.c command_platform.c command.c)
GEFORCE_DISPLAY_STATE_SOURCES := $(addprefix hwdev/gf/display/,hw_state.c hw_state_crtc.c hw_fonts.c hw_load.c hw_pll.c enc_state.c)
GEFORCE_SOURCES := drivers/pci/pci_locate.c kdebug/recover_gf_locate.c  $(filter-out $(GEFORCE_LAYER2_SOURCES),$(wildcard hwdev/gf/*.c)) $(GEFORCE_LAYER2_SOURCES) $(filter-out $(GEFORCE_DISPLAY_STATE_SOURCES),$(wildcard hwdev/gf/display/*.c hwdev/gf/op/*.c)) $(GEFORCE_DISPLAY_STATE_SOURCES) hwdev/gf_load_state.c kdebug/gfinit_decode.c kdebug/gfinit_pll.c kdebug/gfinit_resources.c
GEFORCE_SERVICE_OBJS := $(patsubst %.c,$(GEFORCE_OUT)/%.o,$(GEFORCE_SOURCES))
$(GEFORCE_SERVICE_OBJS): $(GEFORCE_OUT)/%.o: %.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION_MODULE -DGF_PRODUCTION -DGF_PRODUCTION_DECODE $(GEFORCE_INC) -c $< -o $@
GEFORCE_OBJS += $(GEFORCE_SERVICE_OBJS)
$(GEFORCE_OUT)/geforce_import_slots.o $(GEFORCE_OUT)/geforce_provider_types.o $(GEFORCE_OUT)/geforce_kexports.o: $(GEFORCE_OUT)/%.o: $(GEFORCE_OUT)/%.c $(GEFORCE_GEN) build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/geforce_import_stubs.o: $(GEFORCE_OUT)/geforce_import_stubs.S build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(GEFORCE_OUT)/geforce.elf: $(GEFORCE_OBJS) build/geforce-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/geforce-module.ld -Map=$(GEFORCE_OUT)/geforce.map $(GEFORCE_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(GEFORCE_OUT)/geforce_kexports.o
R1_OBJECTS += $(GEFORCE_OUT)/geforce_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(GEFORCE_OUT)/geforce_kexports.o
$(KERNEL_OBJ_PREFIX)drivers/video/geforce/geforce_module.o $(R1_OBJ)/drivers/video/geforce/geforce_module.o: $(GEFORCE_GEN)
$(KERNEL_OBJ_PREFIX)drivers/video/geforce/geforce_module.o $(R1_OBJ)/drivers/video/geforce/geforce_module.o: private CFLAGS_BASE += -I$(GEFORCE_OUT)

# mkmod reads the image/mem/load-span limits from tools/module_schema.py (HAL v2 I3).
define pack_geforce
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 12 --elf $(GEFORCE_OUT)/geforce.elf --metadata $(GEFORCE_OUT)/geforce_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/geforce.mod: $(GEFORCE_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/geforce-module.mk
	$(call pack_geforce,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/geforce.mod: $(GEFORCE_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/geforce-module.mk
	$(call pack_geforce,$(R1_OUT)/vmkernel.lz4)
-include $(GEFORCE_OBJS:.o=.d) $(GEFORCE_OUT)/geforce_kexports.d $(GEFORCE_OUT)/geforce_provider_types.d

# The DMA adapter stays resident and is shared through its typed import.
GEFORCE_DMA_OBJ := drivers/video/geforce/geforce_dma.o
GEFORCE_DMA_KERNEL_OBJ := $(addprefix $(KERNEL_OBJ_PREFIX),$(GEFORCE_DMA_OBJ))
GEFORCE_DMA_R1_OBJ := $(addprefix $(R1_OBJ)/,$(GEFORCE_DMA_OBJ))
C_KERNEL_OBJ += $(GEFORCE_DMA_KERNEL_OBJ)
R1_OBJECTS += $(GEFORCE_DMA_R1_OBJ)
$(BUILD_OUT)/kernel.elf: $(GEFORCE_DMA_KERNEL_OBJ)
$(R1_OUT)/kernel.elf: $(GEFORCE_DMA_R1_OBJ)
$(GEFORCE_DMA_KERNEL_OBJ) $(GEFORCE_DMA_R1_OBJ): $(LAYOUT_HEADER) build/kernel.mk build/config.mk
-include $(GEFORCE_DMA_KERNEL_OBJ:.o=.d) $(GEFORCE_DMA_R1_OBJ:.o=.d)

# Identification is also needed before module loading / ledger freeze.
GEFORCE_BOOT_OBJS := $(GEFORCE_OUT)/boot_locate.o $(GEFORCE_OUT)/boot_pci_locate.o  $(GEFORCE_OUT)/boot_resources.o $(GEFORCE_OUT)/boot_scan.o $(GEFORCE_OUT)/geforce_tick.o
$(GEFORCE_OUT)/geforce_tick.o: drivers/video/geforce/geforce_tick.c drivers/video/geforce/geforce_tick.h build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_resources.o: hwdev/gf/resources.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_scan.o: kdebug/gfinit_resources.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_locate.o: kdebug/recover_gf_locate.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
$(GEFORCE_OUT)/boot_pci_locate.o: drivers/pci/pci_locate.c build/geforce-module.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os -DGF_PRODUCTION $(GEFORCE_INC) -c $< -o $@
C_KERNEL_OBJ += $(GEFORCE_BOOT_OBJS)
R1_OBJECTS += $(GEFORCE_BOOT_OBJS)
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(GEFORCE_BOOT_OBJS)
-include $(GEFORCE_BOOT_OBJS:.o=.d)

# Explicit acceptance target; production objects/images never receive these flags.
# GF_ACCEPT_ID identifies the paired module + gshell measurement build.
GEFORCE_ACCEPT ?=
ifneq ($(GEFORCE_ACCEPT),)
ifeq ($(filter $(GEFORCE_ACCEPT),byte-uc dword-uc dword-wc),)
$(error GEFORCE_ACCEPT must be byte-uc, dword-uc or dword-wc)
endif
ifeq ($(GF_ACCEPT_ID),)
$(error GF_ACCEPT_ID is required for acceptance builds)
endif
GFA_OUT := $(BUILD_OUT)/geforce-accept/$(GEFORCE_ACCEPT)
GFA_MODE := $(if $(filter byte-uc,$(GEFORCE_ACCEPT)),0,$(if $(filter dword-uc,$(GEFORCE_ACCEPT)),1,2))
GFA_OBJS := $(filter-out $(GEFORCE_OUT)/backend_geforce.o $(GEFORCE_OUT)/gf_present.o,$(GEFORCE_OBJS)) $(addprefix $(GFA_OUT)/,backend_geforce.o gf_present.o gf_accept.o gf_accept_cpu.o)
$(GFA_OUT)/build-id: FORCE
	@mkdir -p $(@D)
	@if [ "$$(cat $@ 2>/dev/null)" != '$(GF_ACCEPT_ID)' ]; then echo '$(GF_ACCEPT_ID)' > $@; fi
$(GFA_OUT)/backend_geforce.o: private GFA_FLAGS = -Dgf_present_rect=gf_accept_transfer
$(GFA_OUT)/gf_present.o: private GFA_FLAGS = $(if $(filter byte-uc,$(GEFORCE_ACCEPT)),-DGF_PRESENT_BYTE)
$(GFA_OUT)/gf_accept_cpu.o: arch/$(ARCH)/video/gf_accept_cpu.c $(GEFORCE_GEN) $(GFA_OUT)/build-id build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE -DGF_ACCEPT_MODE=$(GFA_MODE) -DGF_ACCEPT_ID='"$(GF_ACCEPT_ID)"' $(GEFORCE_INC) -c $< -o $@
$(GFA_OUT)/gf_accept.o: private GFA_FLAGS = -DGF_ACCEPT_MODE=$(GFA_MODE) -DGF_ACCEPT_ID='"$(GF_ACCEPT_ID)"' -DGF_ACCEPT_PAIR=$$(python3 -c 'import sys; from pathlib import Path; sys.path.insert(0,"tools"); import mkmod; print(mkmod.pair_crc(Path("$(BUILD_OUT)/vmkernel.lz4").read_bytes()))')
$(GFA_OUT)/gf_accept.o: $(BUILD_OUT)/vmkernel.lz4
$(GFA_OUT)/%.o: drivers/video/geforce/%.c $(GEFORCE_GEN) $(GFA_OUT)/build-id build/config.mk build/geforce-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_GEFORCE_MODULE $(GFA_FLAGS) $(GEFORCE_INC) -c $< -o $@
$(GFA_OUT)/geforce.elf: $(GFA_OBJS) build/geforce-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/geforce-module.ld -Map=$(GFA_OUT)/geforce.map $(GFA_OBJS) -o $@
$(GFA_OUT)/geforce.mod: $(GFA_OUT)/geforce.elf $(GEFORCE_OUT)/geforce_imports.json $(GEFORCE_OUT)/geforce_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 12 --elf $(GFA_OUT)/geforce.elf --metadata $(GEFORCE_OUT)/geforce_imports.json --vmkernel $(BUILD_OUT)/vmkernel.lz4 --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
.PHONY: geforce-accept
geforce-accept: $(GFA_OUT)/geforce.mod
-include $(GFA_OBJS:.o=.d)
endif
