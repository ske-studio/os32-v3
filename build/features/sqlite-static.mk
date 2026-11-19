C_FEATURES += kernel/sqlite_static.c kapi/kapi_db.c lib/shift_di.c lib/softfloat_bits.c $(SOFTFLOAT_RESIDENT_SOURCES)
SQLITE_HARD_C := lib/sqlite3/sqlite3.c lib/sqlite3/os32_sqlite_module.c lib/kmath.c \
    lib/softfloat_abi.c $(SOFTFLOAT_CONV_SOURCES)
