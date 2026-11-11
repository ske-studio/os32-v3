#ifndef OS32_EXEC_LEGACY_H
#define OS32_EXEC_LEGACY_H
#include "types.h"
struct kapi_arg_source;
int legacy_pinned(void);
int legacy_lowmap_busy(void);
int legacy_closing(void);
void legacy_abort_begin(void);
void legacy_end(void);
void legacy_cleanup(void);
int legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src,
                         unsigned count, u32 *result);
#endif
