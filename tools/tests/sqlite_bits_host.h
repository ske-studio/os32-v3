/* Direct host fixtures retain their native SQLite ABI; exercise the real
 * resident bit conversion behind the same integer wire as the module. */
#ifndef SQLITE_BITS_HOST_H
#define SQLITE_BITS_HOST_H
#include "../../lib/sqlite3/sqlite3.h"
#include "../../include/types.h"
#include "../../lib/softfloat/f64_to_i32.c"
#include "../../lib/softfloat/s_roundToI32.c"
#include "../../lib/softfloat/softfloat_raiseFlags.c"
#include "../../lib/softfloat/softfloat_state.c"
#include "../../lib/softfloat_bits.c"
u64 os32_sqlite_column_f64_bits(sqlite3_stmt *stmt, int column)
{
    union { double value; u64 bits; } codec;
    codec.value = sqlite3_column_double(stmt, column);
    return codec.bits;
}
#endif
