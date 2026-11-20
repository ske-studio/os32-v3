# Run from the repository root. This image uses the independent ppc-boot entry.
.DEFAULT_GOAL := kernel
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
ATA_PROBE_OUT ?= build/out/ppc-kboot/ata-probe
CC := $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
NM := $(PPC_CROSS_DIR)/bin/powerpc-elf-nm
CPPFLAGS := -Iinclude -Iarch/ppc -Iplatform/powermac -Ilib -D__cdecl=
CFLAGS := -std=gnu11 -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec \
 -msdata=none -ffreestanding -fno-pie -fno-stack-protector -fno-builtin \
 -fno-asynchronous-unwind-tables -Wall -Wextra -Werror -O2 \
 -ffunction-sections -fdata-sections -MMD -MP
sources := arch/ppc/entry.S arch/ppc/ofw_note.S platform/powermac/ofw.c \
 platform/powermac/escc.c platform/powermac/escc_tree.c lib/kstring_c.c \
 tools/ppc/ata_probe.c
objects := $(addprefix $(ATA_PROBE_OUT)/,$(addsuffix .o,$(basename $(sources))))
elf := $(ATA_PROBE_OUT)/BOOT.ELF

.PHONY: kernel
kernel: $(elf)
	@$(NM) -u $(elf) > $(ATA_PROBE_OUT)/undefined.txt
	@test ! -s $(ATA_PROBE_OUT)/undefined.txt
	@printf 'PASS kboot-ata-probe link\n'

$(elf): $(objects) $(ATA_PROBE_OUT)/boot.ld
	$(CC) -nostdlib -no-pie -Wl,--gc-sections,-N,-z,noexecstack,--no-warn-rwx-segments \
		-Wl,-T,$(ATA_PROBE_OUT)/boot.ld,-Map,$(ATA_PROBE_OUT)/boot.map $(objects) -o $@

$(ATA_PROBE_OUT)/boot.ld: arch/ppc/boot.ld platform/powermac/platform_of_layout.h tools/ppc/ata_probe.mk
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) -include platform_of_layout.h -E -P -x c $< -o $@

$(ATA_PROBE_OUT)/%.o: %.c tools/ppc/ata_probe.mk
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

$(ATA_PROBE_OUT)/%.o: %.S tools/ppc/ata_probe.mk
	@mkdir -p $(@D)
	$(CC) $(CPPFLAGS) $(CFLAGS) -c $< -o $@

-include $(objects:.o=.d)
