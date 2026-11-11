#include "exec_legacy.h"

int exec_legacy_pinned(void) { return 0; }
int exec_legacy_lowmap_busy(void) { return 0; }
int exec_legacy_closing(void) { return 0; }
void exec_legacy_abort_begin(void) { }
void exec_legacy_end(void) { }
void exec_legacy_cleanup(void) { }
int exec_legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src,
                         unsigned count, u32 *result)
{
    (void)slot; (void)fn; (void)src; (void)count; (void)result;
    return 0;
}
