# S0 standalone scaffold; intentionally NOT included by Makefile/kernel.mk.
# Future product integration supplies CFLAGS_BASE, INC_KERNEL and KDEBUG_OBJS.
# Example: make -f build/kdebug.mk kdebug-empty VMKERNEL=build/out/vmkernel.lz4
KDEBUG_OUT ?= build/out/kdebug-s0
KDEBUG_CC ?= $(CROSS_DIR)/bin/i386-elf-gcc
KDEBUG_LD ?= $(CROSS_DIR)/bin/i386-elf-ld
VMKERNEL ?= build/out/vmkernel.lz4
CFLAGS_BASE ?= -std=gnu11 -m32 -march=i386 -ffreestanding -fno-pie -fno-stack-protector
KDEBUG_STAGE ?= S1
KDEBUG_OBJS ?= $(KDEBUG_OUT)/empty.o

.PHONY: kdebug-empty
kdebug-empty: $(KDEBUG_OUT)/kdebug.bin

$(KDEBUG_OUT):
	mkdir -p $@

$(KDEBUG_OUT)/empty.c: | $(KDEBUG_OUT)
	printf '%s\n' 'int kdebug_init(void) { return 0; }' > $@

$(KDEBUG_OUT)/empty.o: $(KDEBUG_OUT)/empty.c
	$(KDEBUG_CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/%.o: kdebug/%.c | $(KDEBUG_OUT)
	$(KDEBUG_CC) $(CFLAGS_BASE) $(INC_KERNEL) -c $< -o $@

$(KDEBUG_OUT)/empty.json: kernel/kdebug_imports.def tools/gen_kdebug_imports.py | $(KDEBUG_OUT)
	python3 -c 'import sys; sys.path.insert(0,"tools"); import gen_kdebug_imports as g; import json; from pathlib import Path; e=g.load_def(); g.generate(e,Path("$(KDEBUG_OUT)"),stage="$(KDEBUG_STAGE)"); p=Path("$(KDEBUG_OUT)/kdebug_imports.json"); m=json.loads(p.read_text()); m["imports"]=[]; Path("$@").write_text(json.dumps(m))'

$(KDEBUG_OUT)/kdebug.elf: $(KDEBUG_OBJS) build/module.ld
	$(KDEBUG_LD) -m elf_i386 --emit-relocs -T build/module.ld $(KDEBUG_OBJS) -o $@

$(KDEBUG_OUT)/kdebug.bin: $(KDEBUG_OUT)/kdebug.elf $(KDEBUG_OUT)/empty.json $(VMKERNEL) tools/mkmod.py
	python3 tools/mkmod.py --elf $< --metadata $(KDEBUG_OUT)/empty.json --vmkernel $(VMKERNEL) -o $@
