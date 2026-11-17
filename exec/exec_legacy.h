#ifndef OS32_EXEC_LEGACY_H
#define OS32_EXEC_LEGACY_H
#include "exec_hooks.h"
#if OS32_HOOK_LEGACY
#include "legacy_guest.h"
static inline int exec_legacy_pinned(void) { return legacy_pinned(); }
static inline int exec_legacy_lowmap_busy(void) { return legacy_lowmap_busy(); }
static inline int exec_legacy_closing(void) { return legacy_closing(); }
static inline void exec_legacy_abort_begin(void) { legacy_abort_begin(); }
static inline void exec_legacy_end(void) { legacy_end(); }
static inline void exec_legacy_cleanup(void) { legacy_cleanup(); }
static inline int exec_legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src,
                                            unsigned count, u32 *result)
{ return legacy_call_dispatch(slot, fn, src, count, result); }
#else
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
#endif
