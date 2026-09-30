# ============================================================================
#  OS32 サードパーティアプリの最小 Makefile
#
#  OS のソースツリーは一切参照しない。配布 SDK だけでビルドする:
#
#    tar xzf os32-sdk-39.tar.gz
#    make OS32_SDK=$(pwd)/os32-sdk-39
#
#  CROSS_DIR はクロスコンパイラの位置 (既定 /usr/local/cross)。
# ============================================================================

# 既定は「この Makefile から見た SDK ルート」。配布 tarball を展開した
# example/hello/ からも、OS のツリー内 sdk/example/hello/ からも通るように
# しておく (前者は ../..、後者は make sdk が配置し直すので同じ)。
OS32_SDK  ?= ../..
CROSS_DIR ?= /usr/local/cross
APP      ?= hello

# このアプリが要求する最低 KernelAPI バージョン。
# SDK の KAPI_VERSION 以下にすること。
API_VER   ?= $(shell cat $(OS32_SDK)/KAPI_VERSION)
HEAP      ?= 262144

CC = i386-elf-gcc
LD = i386-elf-ld
OBJCOPY = i386-elf-objcopy

CFLAGS = -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector \
         -nostdlib -mno-red-zone -fcommon -O2 -Wall -D__OS32_USERLAND__ \
         -I$(OS32_SDK)/include -I$(OS32_SDK)/include/os32 \
         -I$(CROSS_DIR)/i386-elf/include

LDFLAGS = -m elf_i386 -T $(OS32_SDK)/link/app.ld -nostdlib --nmagic --gc-sections \
          -L$(OS32_SDK)/lib -L$(CROSS_DIR)/i386-elf/lib \
          -L$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0

# crt は 4 つとも先頭にリンクする。
CRT  = $(OS32_SDK)/crt/crt0.o $(OS32_SDK)/crt/crt0_c.o \
       $(OS32_SDK)/crt/syscalls.o $(OS32_SDK)/crt/help.o

# 使う SDK ライブラリ。--start-group で囲むので並び順は問わない。
LIBS = -los32gfx -los32math

all: $(APP).bin

$(APP).o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(APP).elf: $(APP).o
	$(LD) $(LDFLAGS) -o $@ $(CRT) $< --start-group $(LIBS) --end-group -lc -lgcc

$(APP).raw: $(APP).elf
	$(OBJCOPY) -O binary $< $@

# OS32X ヘッダを付ける。これを忘れると OS がロードしない。
$(APP).bin: $(APP).raw $(APP).elf
	python3 $(OS32_SDK)/bin/mkos32x.py $(APP).raw $@ \
	        --elf $(APP).elf --api $(API_VER) --heap $(HEAP)

clean:
	rm -f *.o *.elf *.raw *.bin

.PHONY: all clean
