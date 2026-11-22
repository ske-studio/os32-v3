FEP_OUT := $(KB_OUT)/fep
include build/fep-common.mk

ifeq ($(FEATURE_FEP),1)
KB_EXTRA += $(FEP_OUT)/fep_kexports.o
$(KB_OUT)/kernel/ime/fep_module.o $(KB_OUT)/platform/powermac/fep_imports.o: $(FEP_GEN)
$(KB_OUT)/kernel/ime/fep_module.o $(KB_OUT)/platform/powermac/fep_imports.o: private KB_TU_FLAGS += -I$(FEP_OUT)
$(FEP_OUT)/fep_kexports.o: $(FEP_OUT)/fep_kexports.c $(FEP_GEN) $(KB_GEN)/stamp
	$(KB_CC) $(KB_FLAGS) -Ilib/sqlite3 -I$(FEP_OUT) -c $< -o $@
-include $(FEP_OUT)/fep_kexports.d
endif

# The PowerMac default renderer is headless. Its display provider owns all
# platform-side code until the framebuffer renderer is enabled.
PPC_FEP_BODY := $(addprefix $(FEP_OUT)/,$(FEP_BODY_NAMES:=.o))
PPC_FEP_OBJS := $(PPC_FEP_BODY) $(addprefix $(FEP_OUT)/,ime_display.o fep_import_slots.o fep_import_stubs.o)
PPC_FEP_FLAGS := $(KB_FLAGS) -Ilib/sqlite3 -DOS32_FEP_MODULE -I$(FEP_OUT)
PPC_FEP_DEPS := $(FEP_GEN) $(KB_GEN)/stamp build/ppc-fep-module.mk build/targets/ppc-powermac.mk
$(FEP_OUT)/ime_dict.o: private PPC_FEP_FLAGS += -Wno-type-limits
$(PPC_FEP_BODY): $(FEP_OUT)/%.o: kernel/ime/%.c $(PPC_FEP_DEPS)
	$(KB_CC) $(PPC_FEP_FLAGS) -c $< -o $@
$(FEP_OUT)/ime_display.o: platform/powermac/ime_display.c $(PPC_FEP_DEPS)
	$(KB_CC) $(PPC_FEP_FLAGS) -c $< -o $@
$(FEP_OUT)/fep_import_slots.o $(FEP_OUT)/fep_provider_types.o: $(FEP_OUT)/%.o: $(FEP_OUT)/%.c $(PPC_FEP_DEPS)
	$(KB_CC) $(PPC_FEP_FLAGS) -c $< -o $@
$(FEP_OUT)/fep_import_stubs.o: $(FEP_OUT)/fep_import_stubs.S $(PPC_FEP_DEPS)
	$(KB_CC) $(KB_MACHINE) -c $< -o $@
$(FEP_OUT)/fep.elf: $(KB_OUT)/arch/ppc/mmu.o $(PPC_FEP_OBJS) build/ppc-module.ld sdk/link/generations.ld
	$(PPC_CROSS_DIR)/bin/powerpc-elf-ld --emit-relocs --gc-sections --defsym memcpy=kmemcpy --defsym memset=kmemset -u fep_module_ops -e fep_module_init -T build/ppc-module.ld $(filter %.o,$^) -o $@
$(KB_ROOT_FEP): $(FEP_OUT)/fep.elf $(FEP_OUT)/fep_imports.json $(FEP_OUT)/fep_provider_types.o tools/mkmod.py tools/ppc/module_rela.py tools/module_schema.py sdk/target_profile.py sdk/kapi.json sdk/include/os32/os32_generations.h
	python3 -B tools/mkmod.py --target ppc-powermac --module-id 3 --elf $< --metadata $(FEP_OUT)/fep_imports.json --kapi-abi-gen $(shell python3 -c 'import json; print(json.load(open("sdk/kapi.json"))["version"])') --layout-gen $(shell sed -n 's/^\#define OS32_MEMORY_LAYOUT_GENERATION \([0-9]*\).*/\1/p' sdk/include/os32/os32_generations.h) -o $@
-include $(PPC_FEP_OBJS:.o=.d) $(FEP_OUT)/fep_provider_types.d
