#include "cli_boot.h"
#include "lan_boot.h"
#include "fs/vfs.h"
#include "exec/exec.h"
#include "kernel/cli/resident_cli.h"

enum { CLI_CAT_BYTES = 1024 };

static int cli_cat(const char *path)
{
    char buf[CLI_CAT_BYTES];
    int size = vfs_read(path, buf, sizeof(buf));
    if (size < 0) return size;
    pm_cli_write(buf, (u32)size);
    return size;
}

void pm_cli_boot(void)
{
    static const struct resident_cli_ops ops = {
        .getkey = pm_terminal_getkey, .write = pm_cli_write,
        .poll = pm_terminal_poll, .cat = cli_cat, .put = vfs_write,
        .run = exec_run
    };
    resident_cli_run(&ops);
}
