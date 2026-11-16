# PowerPC compiler profile, independent of the x86 CROSS_DIR.
PPC_CROSS_DIR ?= /home/hight/opt/cross-ppc
CC = $(PPC_CROSS_DIR)/bin/powerpc-elf-gcc
AR = $(PPC_CROSS_DIR)/bin/powerpc-elf-ar
AS = $(CC)
LD = $(PPC_CROSS_DIR)/bin/powerpc-elf-ld
OBJCOPY = $(PPC_CROSS_DIR)/bin/powerpc-elf-objcopy
READELF = $(PPC_CROSS_DIR)/bin/powerpc-elf-readelf
CFLAGS_MACHINE = -mcpu=7450 -m32 -mbig-endian -msoft-float -mno-altivec -msdata=none -ffreestanding -fno-pie -fno-stack-protector -nostdlib -fcommon -fsigned-char -fno-short-enums $(DEPFLAGS)
MODULE_LD = $(LD)
MODULE_LDFLAGS = -m elf32ppc
ELF_EMULATION = elf32ppc
# USER uses a separate hard-float ABI; kernel/boot retain 7450 soft-float.
PPC_USER_ABI = -mcpu=750 -mhard-float -mcall-sysv -mno-eabi
PPC_USER_MACHINE = $(filter-out -mcpu=7450 -msoft-float,$(CFLAGS_MACHINE)) $(PPC_USER_ABI)
