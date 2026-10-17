#ifndef OS32_KAPI_STRINGS_H
#define OS32_KAPI_STRINGS_H
#include "types.h"

/* Private input bounds include NUL; no SDK ABI change. */
#define KAPI_STRING_CAP 4096u
#define KAPI_STRING_ARGS 3u
#define KAPI_STRING_WORDS 16u
/* 0: other slot, -1: refused before target, 1: invoked with staged inputs. */
int kapi_strings_call(u32 slot, void *target, const void *user_args,
                      u32 nbytes, u16 ptrmask, u32 *result);
/* Normal return/refusal releases locally; nonlocal STOP/fault releases here,
 * in normal owner teardown, before the AS or slot identity is retired. */
void kapi_strings_cleanup_owned(int app_id);
#endif
