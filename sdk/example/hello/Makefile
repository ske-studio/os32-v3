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
export CROSS_DIR
APP      ?= hello

# このアプリが要求する最低 KernelAPI バージョン。
# SDK の KAPI_VERSION 以下にすること。
API_VER   ?= $(shell cat $(OS32_SDK)/KAPI_VERSION)
HEAP      ?= 262144

# SDK の target (既定 x86-pc98)。CRT・link script・ELF emulation は SDK の profile
# (bin/target_profile.py) からひと組で引く。この SDK で組めない target はここで
# 止まり、コンパイラもリンカも起動しない (override: コマンド行ですり替えさせない)。
OS32_TARGET ?= x86-pc98
# `make clean` だけなら SDK に問い合わせない (SDK が無くても掃除はできる)。
ifneq ($(MAKECMDGOALS),clean)
override OS32_TARGET_TOKENS := $(shell python3 -B $(OS32_SDK)/bin/target_profile_cli.py --target $(OS32_TARGET) --make --root $(OS32_SDK))
ifneq ($(.SHELLSTATUS),0)
$(error OS32 SDK: target $(OS32_TARGET) cannot be built with this SDK)
endif
endif
override os32_comma := ,
override os32_value = $(subst $(os32_comma), ,$(patsubst $(1)=%,%,$(filter $(1)=%,$(OS32_TARGET_TOKENS))))
# target が決める link の引数と CRT。コマンド行から替えられない。
override OS32_TARGET_LINK := -m $(call os32_value,SDK_TARGET_ELF_EMULATION) -T $(call os32_value,SDK_TARGET_APP_LINK)
override CRT := $(call os32_value,SDK_TARGET_CRT_USER)

CC = i386-elf-gcc
LD = python3 $(OS32_SDK)/bin/link_guard.py i386-elf-ld
OBJCOPY = i386-elf-objcopy

CFLAGS = -std=gnu89 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector \
         -nostdlib -mno-red-zone -fcommon -O2 -Wall -D__OS32_USERLAND__ \
         -I$(OS32_SDK)/include -I$(OS32_SDK)/include/os32 \
         -I$(CROSS_DIR)/i386-elf/include

CFLAGS += -include $(OS32_SDK)/include/os32/os32_unit_stamp.h

# SDK が要る link の引数 (検索パスを含む)。利用者の追加の引数は LDFLAGS に書く
# (例: make LDFLAGS=-Map=hello.map)。LDFLAGS を与えてもここは消えない。
override OS32_SDK_LINK := -nostdlib --nmagic --gc-sections \
          -L$(OS32_SDK)/lib -L$(CROSS_DIR)/i386-elf/lib \
          -L$(CROSS_DIR)/lib/gcc/i386-elf/13.2.0
LDFLAGS =

# crt (上の CRT) は profile の並びのまま、全部を先頭にリンクする (x86-pc98 は 4 つ)。

# 使う SDK ライブラリ。--start-group で囲むので並び順は問わない。
LIBS = -los32gfx -los32math

all: $(APP).bin

$(APP).o: main.c
	$(CC) $(CFLAGS) -c $< -o $@

$(APP).elf: $(APP).o
	$(LD) $(strip $(OS32_TARGET_LINK) $(OS32_SDK_LINK) $(LDFLAGS)) -o $@ $(CRT) $< --start-group $(LIBS) --end-group -los32nano -lc -lgcc

$(APP).raw: $(APP).elf
	$(OBJCOPY) -O binary $< $@

# OS32X ヘッダを付ける。これを忘れると OS がロードしない。
$(APP).bin: $(APP).raw $(APP).elf
	python3 $(OS32_SDK)/bin/mkos32x.py $(APP).raw $@ \
	        --elf $(APP).elf --api $(API_VER) --heap $(HEAP)

clean:
	rm -f *.o *.elf *.raw *.bin

.PHONY: all clean
