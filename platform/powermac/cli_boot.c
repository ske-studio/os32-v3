#include "cli_boot.h"
#include "lan_boot.h"
#include "fs/vfs.h"
#include "kernel/cli/resident_cli.h"
#include "kernel/console/console_stream.h"

enum { CLI_CAT_BYTES = 1024 };

static int cli_cat(const char *path)
{
    char buf[CLI_CAT_BYTES];
    int size = vfs_read(path, buf, sizeof(buf));
    if (size < 0) return size;
    console_stream_write(buf, (u32)size);
    return size;
}

void pm_cli_boot(void)
{
    static const struct resident_cli_ops ops = {
        pm_terminal_getkey, console_stream_write, pm_terminal_poll,
        cli_cat, vfs_write
    };
    resident_cli_run(&ops);
}
