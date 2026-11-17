#ifndef OS32_EXEC_KAPI_H
#define OS32_EXEC_KAPI_H
#include "types.h"
/* 契約: 起動時に組む KAPI の関数表の番地 (exec/exec.c が読む)。 */
const u32 *exec_kapi_table(void);
#endif
