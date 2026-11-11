#include "console.h"
#include "kernel/console/console_stream.h"
#include "lan_boot.h"

void shell_putchar(char ch, u8 color)
{
    (void)color;
    console_stream_write(&ch, 1);
    pm_terminal_poll();
}
