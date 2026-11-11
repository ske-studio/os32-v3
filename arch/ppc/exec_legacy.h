#ifndef OS32_EXEC_LEGACY_H
#define OS32_EXEC_LEGACY_H
#include "types.h"
struct kapi_arg_source;
int exec_legacy_pinned(void);
int exec_legacy_lowmap_busy(void);
int exec_legacy_closing(void);
void exec_legacy_abort_begin(void);
void exec_legacy_end(void);
void exec_legacy_cleanup(void);
int exec_legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src,
                         unsigned count, u32 *result);
#endif
