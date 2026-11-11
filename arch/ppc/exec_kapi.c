#include "exec_kapi.h"
#include "kapi_table.h"
#include "os32_kapi_slots.h"

/* F15 supplies the initialized table and its data fields. */
const u32 ppc_kapi_table[KAPI_FUNC_CAPACITY + 2] = { 0 };
const u32 *exec_kapi_table(void) { return ppc_kapi_table; }
