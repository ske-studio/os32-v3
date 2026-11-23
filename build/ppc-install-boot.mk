# The HDD executable shares the selected product objects except for the entry,
# root selection and media-specific pair provider.
HD_DIR := $(KB_OUT)/hdd
HD_SPECIAL := platform/powermac/apm.o arch/ppc/kernel_entry.o platform/powermac/kboot_capture.o \
 platform/powermac/ide_tree.o platform/powermac/partition_view.o platform/powermac/ppc_kernel.o
HD_SHARED := $(filter-out $(addprefix $(KB_OUT)/,$(HD_SPECIAL) platform/powermac/module_pair.o),$(KB_OBJ))
HD_OBJECTS := $(addprefix $(HD_DIR)/,$(HD_SPECIAL) platform/powermac/product_boot.o)
HD_FLAGS := $(filter-out -DPM_ROOT_FRAME=%,$(KB_FLAGS)) -DPM_PRODUCT_BOOT=1 -DPM_ROOT_FRAME=0
$(HD_DIR)/%.o: %.c $(KB_GEN)/stamp $(KB_OUT)/memmap_layout.h $(KB_SELECT_MK) build/ppc-install-boot.mk
	@mkdir -p $(@D)
	$(KB_CC) $(HD_FLAGS) -c $< -o $@
$(HD_DIR)/%.o: %.S $(KB_OUT)/memmap_layout.h build/ppc-install-boot.mk
	@mkdir -p $(@D)
	$(KB_CC) $(HD_FLAGS) -c $< -o $@
$(HD_DIR)/kernel.ld: arch/ppc/kernel.ld $(KB_OUT)/memmap_layout.h
	@mkdir -p $(@D)
	$(KB_CC) $(HD_FLAGS) -MT $@ -MF $@.d -E -P -x c $< -o $@
$(HD_DIR)/PRODUCT.ELF: $(KB_ELF) $(HD_SHARED) $(HD_OBJECTS) $(KB_EXTRA) $(HD_DIR)/kernel.ld tools/ppc/product_media.py
	@set -e; pair=$$(python3 -B tools/ppc/product_media.py pair "$(KB_ELF)"); \
	$(KB_CC) $(KB_MACHINE) -no-pie -Wl,--gc-sections,--no-undefined,-z,noexecstack \
	-Wl,--defsym,kboot_tick=tick_count,--defsym,pm_distribution_pair=$$pair $(KB_KEEP_SQLITE) $(KB_KEEP_PAIR) \
	-Wl,-T,$(HD_DIR)/kernel.ld $(HD_OBJECTS) $(HD_SHARED) $(KB_EXTRA) -o $@
$(HD_DIR)/loader.ld: arch/ppc/boot.ld platform/powermac/platform_of_layout.h
	@mkdir -p $(@D)
	$(KB_CC) -E -P -x c -imacros platform/powermac/platform_of_layout.h $< -o $@
$(HD_DIR)/primitives.o: $(KB_OUT)/arch/ppc/entry.o
	$(KB_OBJCOPY) --localize-symbol=_start $< $@
HD_LOADER := $(addprefix $(HD_DIR)/,platform/powermac/of_loader_entry.o platform/powermac/of_loader.o arch/ppc/ofw_note.o primitives.o)
$(HD_DIR)/LOADER.ELF: $(HD_LOADER) $(HD_DIR)/loader.ld $(addprefix $(KB_OUT)/,platform/powermac/ofw.o lib/kstring.o lib/kstring_c.o)
	$(KB_CC) $(KB_MACHINE) -no-pie -Wl,--gc-sections,-T,$(HD_DIR)/loader.ld $(filter %.o,$^) -o $@
$(HD_DIR)/BOOT.HFS: $(HD_DIR)/LOADER.ELF $(HD_DIR)/PRODUCT.ELF tools/ppc/product_media.py
	python3 -B tools/ppc/product_media.py hfs $@ $(filter %.ELF,$^)
HD_MEDIA_FILES := $(HD_DIR)/BOOT.HFS $(KB_OUT)/root.iso $(HD_DIR)/LOADER.ELF $(HD_DIR)/PRODUCT.ELF
HD_PACKAGES := $(addprefix $(HD_DIR)/,PLAN.BIN BOOT.PKG MINIMAL.PKG)
$(HD_PACKAGES) &: $(HD_MEDIA_FILES) tools/ppc/install_packages.py tools/mkpkg.py tools/mkmod.py tools/module_schema.py sdk/include/os32/os32_kapi_shared.h
	python3 -B tools/ppc/install_packages.py $(HD_MEDIA_FILES)
$(BOOT_ISO): $(HD_PACKAGES) $(HD_MEDIA_FILES)
HD_MEDIA_ARGS := $(HD_MEDIA_FILES) $(HD_PACKAGES)
-include $(HD_OBJECTS:.o=.d) $(HD_LOADER:.o=.d) $(HD_DIR)/kernel.ld.d
