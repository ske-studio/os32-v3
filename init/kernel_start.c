/* カーネルの入口の骨格 (機種に依らない 1 本)。局面の中身は platform が持つ。 */
#include "kernel_start.h"
#include "arch_io.h"

void kernel_start(void)
{
    platform_boot_machine();
    _enable();
    platform_boot_services();
    platform_boot_run();
    for (;;) _halt();
}
