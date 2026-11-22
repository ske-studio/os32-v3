# T5a-2: reuse T4 function imports, VA0 packing and paired distribution.
FEP_OUT := $(BUILD_OUT)/fep
include build/fep-common.mk

FEP_OBJS := $(addprefix $(FEP_OUT)/,$(FEP_BODY_NAMES:=.o) $(FEP_DISPLAY_NAMES:=.o) fep_import_slots.o fep_import_stubs.o)
FEP_INC := $(INC_KERNEL) -I. -I$(FEP_OUT)
$(addprefix $(FEP_OUT)/,$(FEP_BODY_NAMES:=.o)): $(FEP_OUT)/%.o: kernel/ime/%.c $(FEP_GEN) build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_FEP_MODULE $(FEP_INC) -c $< -o $@
# Display implementation remains private to the FEP module, with its basename.
$(addprefix $(FEP_OUT)/,$(FEP_DISPLAY_NAMES:=.o)): $(FEP_OUT)/%.o: platform/$(PLATFORM)/%.c $(FEP_GEN) build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_FEP_MODULE $(FEP_INC) -c $< -o $@
# 2026-11-10 第 5 段 C2 で platform/pc98 へ移設。差分ビルドの古い .d が旧パスを指すときの対策 (次の大きな整理で消してよい)。
kernel/ime/ime_render_tvram.c: ;
$(FEP_OUT)/fep_import_slots.o $(FEP_OUT)/fep_provider_types.o $(FEP_OUT)/fep_kexports.o: $(FEP_OUT)/%.o: $(FEP_OUT)/%.c $(FEP_GEN) build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(FEP_INC) -c $< -o $@
$(FEP_OUT)/fep_import_stubs.o: $(FEP_OUT)/fep_import_stubs.S build/config.mk build/fep-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(FEP_OUT)/fep.elf: $(FEP_OBJS) build/fep-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/fep-module.ld -Map=$(FEP_OUT)/fep.map $(FEP_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(FEP_OUT)/fep_kexports.o
R1_OBJECTS += $(FEP_OUT)/fep_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(FEP_OUT)/fep_kexports.o
$(KERNEL_OBJ_PREFIX)kernel/ime/fep_module.o $(R1_OBJ)/kernel/ime/fep_module.o: $(FEP_GEN)
$(KERNEL_OBJ_PREFIX)kernel/ime/fep_module.o $(R1_OBJ)/kernel/ime/fep_module.o: private CFLAGS_BASE += -I$(FEP_OUT)

define pack_fep
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 3 --elf $(FEP_OUT)/fep.elf --metadata $(FEP_OUT)/fep_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/fep.mod: $(FEP_OUT)/fep.elf $(FEP_OUT)/fep_imports.json $(FEP_OUT)/fep_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/fep-module.mk
	$(call pack_fep,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/fep.mod: $(FEP_OUT)/fep.elf $(FEP_OUT)/fep_imports.json $(FEP_OUT)/fep_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/fep-module.mk
	$(call pack_fep,$(R1_OUT)/vmkernel.lz4)
-include $(FEP_OBJS:.o=.d) $(FEP_OUT)/fep_kexports.d $(FEP_OUT)/fep_provider_types.d
