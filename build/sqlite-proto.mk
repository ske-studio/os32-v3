# T4a measured VA0 body, now shared by the paired product and measurement tests.
SQLITE_PROTO_OUT ?= $(BUILD_OUT)/sqlite-proto
SQLITE_PROTO_GEN := $(addprefix $(SQLITE_PROTO_OUT)/,sqlite_imports.h sqlite_imports.json sqlite_import_slots.c sqlite_import_stubs.S sqlite_provider_types.c os32_sqlite_vfs.c sqlite_facade.h sqlite_outer_calls.inc sqlite_facade_bind.inc sqlite_kexports.c)
SQLITE_PROTO_DEF_SOURCES := fs/vfs.h include/kprintf.h include/sys.h kernel/kmalloc.h
$(SQLITE_PROTO_GEN) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/sqlite_imports.def lib/sqlite3/os32_sqlite_vfs.c $(SQLITE_PROTO_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --out-dir $(SQLITE_PROTO_OUT)

SQLITE_PROTO_OBJS := $(addprefix $(SQLITE_PROTO_OUT)/,sqlite3.o os32_sqlite_vfs.o sqlite_module.o sqlite_import_slots.o sqlite_import_stubs.o kstring.o kstring_c.o kmath.o)
SQLITE_PROTO_INC := $(INC_SQLITE) -I. -I$(SQLITE_PROTO_OUT)
$(SQLITE_PROTO_OUT)/sqlite3.o: lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_config.h build/config.mk build/sqlite-proto.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_SQLITE) -include lib/sqlite3/os32_sqlite_config.h $(SQLITE_PROTO_INC) -c $< -o $@
$(SQLITE_PROTO_OUT)/os32_sqlite_vfs.o: $(SQLITE_PROTO_OUT)/os32_sqlite_vfs.c $(SQLITE_PROTO_OUT)/sqlite_imports.h lib/sqlite3/os32_sqlite_vfs.h lib/sqlite3/os32_sqlite_config.h build/config.mk build/sqlite-proto.mk
	$(CC) $(CFLAGS_SQLITE) -include lib/sqlite3/os32_sqlite_config.h $(SQLITE_PROTO_INC) -c $< -o $@
$(SQLITE_PROTO_OUT)/sqlite_module.o: lib/sqlite3/os32_sqlite_module.c lib/sqlite3/os32_sqlite_vfs.h build/config.mk build/sqlite-proto.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os $(SQLITE_PROTO_INC) -c $< -o $@
$(SQLITE_PROTO_OUT)/sqlite_import_slots.o $(SQLITE_PROTO_OUT)/sqlite_provider_types.o: $(SQLITE_PROTO_OUT)/%.o: $(SQLITE_PROTO_OUT)/%.c $(SQLITE_PROTO_OUT)/sqlite_imports.h build/config.mk build/sqlite-proto.mk
	$(CC) $(CFLAGS_BASE) -Os -Werror $(SQLITE_PROTO_INC) -c $< -o $@
$(SQLITE_PROTO_OUT)/sqlite_import_stubs.o: $(SQLITE_PROTO_OUT)/sqlite_import_stubs.S build/config.mk build/sqlite-proto.mk
	$(CC) $(CFLAGS_MACHINE) -c $< -o $@
$(SQLITE_PROTO_OUT)/kstring.o $(SQLITE_PROTO_OUT)/kstring_c.o $(SQLITE_PROTO_OUT)/kmath.o: $(SQLITE_PROTO_OUT)/%.o: lib/%.c build/config.mk build/sqlite-proto.mk
	mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os $(INC_LIB) -c $< -o $@
$(SQLITE_PROTO_OUT)/sqlite.elf: $(SQLITE_PROTO_OBJS) build/sqlite-module.ld sdk/link/generations.ld
	$(CROSS_DIR)/bin/i386-elf-ld -m elf_i386 --emit-relocs -T build/sqlite-module.ld -Map=$(SQLITE_PROTO_OUT)/sqlite.map $(SQLITE_PROTO_OBJS) $(shell $(CC) -print-libgcc-file-name) -o $@
# Measurement consumes the already built VK32 without invoking its FORCE
# relink (which would stale distribution hashes while host checks run).
$(SQLITE_PROTO_OUT)/sqlite.bin: .FORCE $(SQLITE_PROTO_OUT)/sqlite.elf $(SQLITE_PROTO_OUT)/sqlite_imports.json tools/sqlite_module_proto.py tools/mkmod.py tools/module_schema.py sdk/kapi.json
	python3 tools/sqlite_module_proto.py --directory $(SQLITE_PROTO_OUT) --vmkernel $(BUILD_OUT)/vmkernel.lz4

sqlite-module-proto: $(SQLITE_PROTO_OUT)/sqlite.bin $(SQLITE_PROTO_OUT)/sqlite_provider_types.o
.PHONY: sqlite-module-proto
-include $(SQLITE_PROTO_OBJS:.o=.d) $(SQLITE_PROTO_OUT)/sqlite_provider_types.d

# Reuse the measured T4a body and generator for the production component.
SQLITE_GEN := lib/sqlite3/generated
SQLITE_GENERATED := $(addprefix $(SQLITE_GEN)/,sqlite_imports.h sqlite_facade.h sqlite_outer_calls.inc sqlite_facade_bind.inc sqlite_kexports.c)
$(SQLITE_GENERATED) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/sqlite_imports.def lib/sqlite3/os32_sqlite_vfs.c $(SQLITE_PROTO_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --out-dir $(SQLITE_GEN) --contracts-only

SQLITE_KEXPORT_OBJ := $(SQLITE_PROTO_OUT)/sqlite_kexports.o
$(SQLITE_KEXPORT_OBJ): $(SQLITE_GEN)/sqlite_kexports.c $(SQLITE_GEN)/sqlite_imports.h
	@mkdir -p $(@D)
	$(CC) $(CFLAGS_BASE) -Os $(INC_KERNEL) -I. -I$(SQLITE_GEN) -c $< -o $@
C_KERNEL_OBJ += $(SQLITE_KEXPORT_OBJ)
R1_OBJECTS += $(SQLITE_KEXPORT_OBJ)
$(BUILD_OUT)/kernel.elf $(R1_OUT)/kernel.elf: $(SQLITE_KEXPORT_OBJ)
$(KERNEL_OBJ_PREFIX)kernel/sqlite_engine.o $(KERNEL_OBJ_PREFIX)kernel/sqlite_outer.o $(R1_OBJ)/kernel/sqlite_engine.o $(R1_OBJ)/kernel/sqlite_outer.o: $(SQLITE_GENERATED)
$(SQLITE_PROTO_OUT)/sqlite_module.o: $(SQLITE_GENERATED)

define pack_sqlite
	python3 tools/mkmod.py --module-id 2 --elf $(SQLITE_PROTO_OUT)/sqlite.elf --metadata $(SQLITE_PROTO_OUT)/sqlite_imports.json --vmkernel $(1) --kapi-abi-gen $(KDEBUG_KAPI_GEN) --layout-gen $(KDEBUG_LAYOUT_GEN) -o $@
endef
$(BUILD_OUT)/sqlite.mod: $(SQLITE_PROTO_OUT)/sqlite.elf $(SQLITE_PROTO_OUT)/sqlite_imports.json $(BUILD_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/sqlite-proto.mk
	$(call pack_sqlite,$(BUILD_OUT)/vmkernel.lz4)
$(R1_OUT)/sqlite.mod: $(SQLITE_PROTO_OUT)/sqlite.elf $(SQLITE_PROTO_OUT)/sqlite_imports.json $(R1_OUT)/vmkernel.lz4 tools/mkmod.py tools/module_schema.py sdk/kapi.json build/sqlite-proto.mk
	$(call pack_sqlite,$(R1_OUT)/vmkernel.lz4)
-include $(SQLITE_KEXPORT_OBJ:.o=.d)
