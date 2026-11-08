#include "bios_snapshot.h"
#include "../../mm/paging.h"
#include "kstring.h"
#include "io.h"

/* Immutable platform boot page, independent of optional legacy sessions. */
static int real_saved;
static u8 real_lowmem[PAGE_SIZE];
int platform_bios_ready(void)
{
    return real_saved;
}
#define REAL_SNAPSHOT_SIZE PAGE_SIZE
void v86_bios_save_real(void)
{
    if (real_saved) return;
    kmemcpy(real_lowmem, P2V_BOOT(0), REAL_SNAPSHOT_SIZE);
    real_saved = 1;
}

void v86_bios_restore_real(void)
{
    if (!real_saved) return;
    if (paging_set_page(0, 0, PAGE_RW) != 0) for (;;) _stop();
    kmemcpy(P2V(0), real_lowmem, REAL_SNAPSHOT_SIZE);
}

const u8 *v86_bios_real_lowmem(void)
{
    return real_lowmem;
}
