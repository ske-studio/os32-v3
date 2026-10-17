# T4a-2 opt-in instrument, independent of all/kernel and deployment.
SQLITE_PROTO_OUT ?= $(BUILD_OUT)/sqlite-proto
SQLITE_PROTO_GEN := $(addprefix $(SQLITE_PROTO_OUT)/,sqlite_imports.h sqlite_imports.json sqlite_import_slots.c sqlite_import_stubs.S sqlite_provider_types.c os32_sqlite_vfs.c)
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
$(SQLITE_PROTO_OUT)/sqlite_module.o: tools/prototypes/sqlite_module.c lib/sqlite3/os32_sqlite_vfs.h build/config.mk build/sqlite-proto.mk
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
$(SQLITE_PROTO_OUT)/sqlite.bin: $(SQLITE_PROTO_OUT)/sqlite.elf $(SQLITE_PROTO_OUT)/sqlite_imports.json tools/sqlite_module_proto.py tools/mkmod.py tools/module_schema.py sdk/kapi.json
	python3 tools/sqlite_module_proto.py --directory $(SQLITE_PROTO_OUT)

sqlite-module-proto: $(SQLITE_PROTO_OUT)/sqlite.bin $(SQLITE_PROTO_OUT)/sqlite_provider_types.o
.PHONY: sqlite-module-proto
-include $(SQLITE_PROTO_OBJS:.o=.d) $(SQLITE_PROTO_OUT)/sqlite_provider_types.d
