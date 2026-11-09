# Type contracts are evaluated on every x86 build without producing artifacts.
# Keep this include after the product graph: profiles and object lists are final.
ifeq ($(ARCH),x86)
.PHONY: abi-contract-probe
abi-contract-probe: | $(KAPI_GENERATED) $(LAYOUT_HEADER)
	$(CC) $(KERNEL_CFLAGS) -Iarch/$(ARCH) -Iinclude -MF /dev/null -fsyntax-only tools/abi/type_contract.c
	$(CC) $(PROGRAM_FLAGS) -Iarch/$(ARCH) -Iinclude -MF /dev/null -fsyntax-only tools/abi/type_contract.c
	$(CC) $(CFLAGS_SQLITE) -Iarch/$(ARCH) -Iinclude -MF /dev/null -fsyntax-only tools/abi/type_contract.c
	$(CC) $(SQLITE_SA_CFLAGS) -Iarch/$(ARCH) -Iinclude -fsyntax-only tools/abi/type_contract.c
	$(CC) $(CFLAGS_BOOT) -Iarch/$(ARCH) -Iinclude -fsyntax-only tools/abi/type_contract.c
	$(CC) $(PROGRAM_FLAGS) -std=gnu89 -Iarch/$(ARCH) -Iinclude -MF /dev/null -fsyntax-only -x c sdk/include/os32/os32_kapi_shared.h
	$(CC) $(PROGRAM_FLAGS) -std=gnu89 -Iarch/$(ARCH) -Iinclude -MF /dev/null -fsyntax-only -x c sdk/include/os32/os32_gui_shared.h

# Order-only edges avoid relinking unchanged products. Concrete lists cover
# kernel/module/boot rules; userland C rules carry their own order-only edge.
# These objects are side outputs of the generic .elf rules, and can also
# be requested directly through make's built-in C rule. Enumerate concrete
# targets: a recipe-less pattern rule would not augment that rule.
ABI_DIRECT_USER_OBJECTS := $(patsubst %.c,%.o,$(wildcard userland/cmds/*.c \
    userland/tests/*.c userland/tests/bench/*.c userland/system/*.c))
$(ABI_DIRECT_USER_OBJECTS) $(filter %.o,$(ALL_TARGET_OBJECTS)) $(BOOT_C_OBJ) $(BOOT_DBG_ALL_OBJ) \
$(BUILD_OUT)/tickprobe.elf $(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf all kernel kernel-r1 programs sdk: | abi-contract-probe
endif
