#include "platform_boot.h"
#include "bootinfo.h"
/* OF argument validation is supplied by a later stage; no BIOS geometry. */
void platform_boot_capture(void)
{
    const struct boot_common none = { 0, 0, 0 };
    boot_common_set(&none, 0);
}
