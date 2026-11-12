# USER-only PowerPC applications use the selected SDK CRT and linker script.
ifeq ($(SDK_TARGET_CALL_ABI),powerpc-sysv)
PPC_USER_OUT := build/out/ppc-user
PPC_USER_CRT := $(addprefix $(PPC_USER_OUT)/,$(notdir $(SDK_TARGET_CRT_USER)))
PPC_USER_FLAGS := $(USER_CFLAGS) -fno-common -mcall-sysv -mno-eabi -I sdk/include/os32 -Wno-attributes

PPC_USER_APPS := $(addprefix $(PPC_USER_OUT)/,hello fault)

.PHONY: hello ppc-fault
hello: $(PPC_USER_OUT)/hello.bin
ppc-fault: $(PPC_USER_OUT)/fault.bin

$(PPC_USER_OUT):
	mkdir -p $@

# Assembly has no C unit stamp; emit the same generation note alongside it.
$(PPC_USER_OUT)/crt0.o: sdk/crt/ppc/crt0.S sdk/os32x_hdr.py | $(PPC_USER_OUT)
	python3 -B -c 'import sys; sys.path.insert(0,"sdk"); import os32x_hdr as h; print(".include \"sdk/crt/ppc/crt0.S\"\n.section .os32_generations,\"\",@progbits\n.long %d,%d,%d,%d" % (h.OS32X_HDR_VERSION,h.OS32_KAPI_ABI_GENERATION,h.OS32_MEMORY_LAYOUT_GENERATION,h.OS32_SHLIB_PROTOCOL))' > $(PPC_USER_OUT)/crt0-stamped.S
	$(CC) $(CFLAGS_MACHINE) -mcall-sysv -mno-eabi -c $(PPC_USER_OUT)/crt0-stamped.S -o $@

$(PPC_USER_OUT)/crt0_c.o: sdk/crt/ppc/start.c | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(addsuffix .o,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.o: userland/tests/ppc_%.c | $(PPC_USER_OUT)
	$(CC) $(PPC_USER_FLAGS) -c $< -o $@

$(addsuffix .elf,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.elf: $(PPC_USER_CRT) $(PPC_USER_OUT)/%.o $(SDK_TARGET_APP_LINK) sdk/link_guard.py
	python3 sdk/link_guard.py --target $(SDK_TARGET) $(LD) -m $(SDK_TARGET_ELF_EMULATION) --gc-sections -T $(SDK_TARGET_APP_LINK) -o $@ $(filter %.o,$^)

$(addsuffix .raw,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.raw: $(PPC_USER_OUT)/%.elf
	$(OBJCOPY) -O binary $< $@

$(addsuffix .bin,$(PPC_USER_APPS)): $(PPC_USER_OUT)/%.bin: $(PPC_USER_OUT)/%.raw $(PPC_USER_OUT)/%.elf sdk/mkos32x.py
	python3 sdk/mkos32x.py $< $@ --target $(SDK_TARGET) --elf $(PPC_USER_OUT)/$*.elf

-include $(PPC_USER_CRT:.o=.d) $(PPC_USER_OUT)/hello.d $(PPC_USER_OUT)/fault.d
endif
