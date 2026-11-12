#include "exec_kapi.h"
#include "kapi_table.h"

_Static_assert(sizeof(KernelAPI) >= KAPI_DATA_FIELDS_OFF + 2 * sizeof(u32),
               "KAPI table must include its data fields");
KernelAPI ppc_kapi_table __attribute__((aligned(4096)));
const u32 *exec_kapi_table(void) { return (const u32 *)&ppc_kapi_table; }
