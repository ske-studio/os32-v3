#include "exec_legacy.h"

int legacy_pinned(void) { return 0; }
int legacy_lowmap_busy(void) { return 0; }
int legacy_closing(void) { return 0; }
void legacy_abort_begin(void) { }
void legacy_end(void) { }
void legacy_cleanup(void) { }
int legacy_call_dispatch(u32 slot, void *fn, struct kapi_arg_source *src,
                         unsigned count, u32 *result)
{
    (void)slot; (void)fn; (void)src; (void)count; (void)result;
    return 0;
}
