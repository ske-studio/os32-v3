# x86 toolchain and machine ABI; preserve command-line overrides.
# Tools
CC = i386-elf-gcc
AR = i386-elf-ar
AS = nasm
LD = python3 sdk/link_guard.py --target $(ARCH)-$(PLATFORM) --ar $(AR) i386-elf-ld
OBJCOPY = i386-elf-objcopy

CFLAGS_MACHINE = -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector \
                 -nostdlib -mno-red-zone -fcommon -fsigned-char -fno-short-enums $(DEPFLAGS)
MODULE_LD = $(CROSS_DIR)/bin/i386-elf-ld
MODULE_LDFLAGS = -m elf_i386
ELF_EMULATION = elf_i386
