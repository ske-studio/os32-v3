# T5c-2b: reuse T4 function imports, VA0 packing and paired distribution.
PCM_OUT := $(BUILD_OUT)/pcm
PCM_GEN := $(addprefix $(PCM_OUT)/,pcm_imports.h pcm_imports.json pcm_import_slots.c pcm_import_stubs.S pcm_provider_types.c pcm_kexports.c)
PCM_DEF_SOURCES := sys/hal/sound/snd2_int.h sdk/include/os32/os32_snd2.h sys/include/pcm_contract.h sys/include/dma_contract.h sys/dev/sound/cs4231/pcm_module.h sys/dev/sound/cs4231/pcm_cs4231.h sys/kern/irq/irq.h sys/kern/mm/dma_pool.h sys/hal/time/tick_clock.h
$(PCM_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/pcm_imports.def $(PCM_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --module pcm --out-dir $(PCM_OUT)

PCM_OBJS := $(addprefix $(PCM_OUT)/,pcm_body.o pcm_hw.o pcm_cs4231.o pcm_cs4231_math.o pcm_import_slots.o pcm_import_stubs.o)
ifeq ($(TICK_PROBE_BUILD),1)
$(PCM_OUT)/pcm_hw.o $(PCM_OUT)/pcm_cs4231.o: private PCM_DIAG_FLAG := -DSND2_REFILL_DIAG
endif
PCM_INC := $(INC_KERNEL) -I. -I$(PCM_OUT)
$(PCM_OUT)/pcm_body.o: sys/dev/sound/cs4231/pcm_body.c $(PCM_GEN) build/config.mk build/pcm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_PCM_MODULE $(PCM_INC) -c $< -o $@
$(PCM_OUT)/pcm_hw.o: sys/dev/sound/cs4231/pcm_hw.c $(PCM_GEN) build/config.mk build/pcm-module.mk
	$(CC) $(PCM_DIAG_FLAG) $(CFLAGS_BASE) -Os -DOS32_PCM_MODULE $(PCM_INC) -c $< -o $@
$(PCM_OUT)/pcm_cs4231.o: sys/dev/sound/cs4231/pcm_cs4231.c $(PCM_GEN) build/config.mk build/pcm-module.mk
	$(CC) $(PCM_DIAG_FLAG) $(CFLAGS_BASE) -Os -DOS32_PCM_MODULE $(PCM_INC) -c $< -o $@
$(PCM_OUT)/pcm_cs4231_math.o: sys/dev/sound/cs4231/pcm_cs4231_math.c $(PCM_GEN) build/config.mk build/pcm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -DOS32_PCM_MODULE $(PCM_INC) -c $< -o $@
$(PCM_OUT)/pcm_import_slots.o $(PCM_OUT)/pcm_provider_types.o $(PCM_OUT)/pcm_kexports.o: $(PCM_OUT)/%.o: $(PCM_OUT)/%.c $(PCM_GEN) build/config.mk build/pcm-module.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(PCM_INC) -c $< -o $@
$(PCM_OUT)/pcm_import_stubs.o: $(PCM_OUT)/pcm_import_stubs.S build/config.mk build/pcm-module.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(PCM_OUT)/pcm.elf: $(PCM_OBJS) build/pcm-module.ld sdk/link/generations.ld
	$(MODULE_LD) $(MODULE_LDFLAGS) --emit-relocs -T build/pcm-module.ld -Map=$(PCM_OUT)/pcm.map $(PCM_OBJS) -o $@

# Kernel and R1 both publish exactly the generated function contracts.
C_KERNEL_OBJ += $(PCM_OUT)/pcm_kexports.o
R1_OBJECTS += $(PCM_OUT)/pcm_kexports.o
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(PCM_OUT)/pcm_kexports.o
$(KERNEL_OBJ_PREFIX)sys/dev/sound/cs4231/pcm_module.o $(R1_OBJ)/sys/dev/sound/cs4231/pcm_module.o: $(PCM_GEN)
$(KERNEL_OBJ_PREFIX)sys/dev/sound/cs4231/pcm_module.o $(R1_OBJ)/sys/dev/sound/cs4231/pcm_module.o: private CFLAGS_BASE += -I$(PCM_OUT)

define pack_pcm
	python3 tools/mkmod.py --target $(ARCH)-$(PLATFORM) --module-id 7 --elf $(PCM_OUT)/pcm.elf --metadata $(PCM_OUT)/pcm_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/pcm.mod: $(PCM_OUT)/pcm.elf $(PCM_OUT)/pcm_imports.json $(PCM_OUT)/pcm_provider_types.o $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/pcm-module.mk
	$(call pack_pcm,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/pcm.mod: $(PCM_OUT)/pcm.elf $(PCM_OUT)/pcm_imports.json $(PCM_OUT)/pcm_provider_types.o $(R1_OUT)/vmkernel.lz4 tools/mkmod.py sdk/target_profile.py tools/module_schema.py sdk/kapi.json build/pcm-module.mk
	$(call pack_pcm,$(R1_OUT)/vmkernel.lz4)
-include $(PCM_OBJS:.o=.d) $(PCM_OUT)/pcm_kexports.d $(PCM_OUT)/pcm_provider_types.d
