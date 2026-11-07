# PowerPC compiler profile, independent of the x86 CROSS_DIR.
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
CC = $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
AR = $(PPC_CROSS_DIR)/bin/powerpc-elf-ar
AS = $(CC)
LD = $(PPC_CROSS_DIR)/bin/powerpc-elf-ld
OBJCOPY = $(PPC_CROSS_DIR)/bin/powerpc-elf-objcopy
CFLAGS_MACHINE = -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon -fsigned-char -fno-short-enums $(DEPFLAGS)
MODULE_LD = $(LD)
MODULE_LDFLAGS = -m elf32ppc
ELF_EMULATION = elf32ppc
