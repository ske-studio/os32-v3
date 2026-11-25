SQLITE_PROTO_DEF_SOURCES := sys/fs/vfs.h sys/include/kprintf.h sys/include/sys.h sys/kern/mm/kmalloc.h
SQLITE_GEN := lib/sqlite3/generated
SQLITE_GENERATED := $(addprefix $(SQLITE_GEN)/,sqlite_imports.h sqlite_facade.h sqlite_outer_calls.inc sqlite_facade_bind.inc sqlite_kexports.c)
$(SQLITE_GENERATED) &: tools/gen_sqlite_imports.py tools/gen_kdebug_imports.py tools/prototypes/sqlite_imports.def lib/sqlite3/os32_sqlite_vfs.c $(SQLITE_PROTO_DEF_SOURCES)
	python3 tools/gen_sqlite_imports.py --out-dir $(SQLITE_GEN) --contracts-only
