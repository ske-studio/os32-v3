#include "v86_mem.h"
#include "v86_bios.h"
#include "paging.h"
#include "kstring.h"

/* The one immutable page0 copy, captured before paging/low-memory reuse.
 * Keep it in the existing core session storage for T7a-1c; never reset it. */
#define REAL_SNAPSHOT_SIZE PAGE_SIZE
void v86_bios_save_real(void)
{
    if (v86_session.real_saved) return;
    kmemcpy(v86_session.real_lowmem, P2V_BOOT(0), REAL_SNAPSHOT_SIZE);
    v86_session.real_saved = 1;
}

void v86_bios_restore_real(void)
{
    if (!v86_session.real_saved) return;
    paging_set_page(0, 0, PAGE_RW);
    kmemcpy(P2V(0), v86_session.real_lowmem, REAL_SNAPSHOT_SIZE);
}

const u8 *v86_bios_real_lowmem(void)
{
    return v86_session.real_lowmem;
}
