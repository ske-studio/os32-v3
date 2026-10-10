/* CPL3 pipe opacity. The child runs sequentially under another owner.
 * Nonzero lengths are seeded in the host test: there is no public setter.
 * sys_redirect_fd_buf registers separate USER storage, not a pipe ID, and
 * neither it nor sys_write updates pipe_len. USER get_buf is always NULL. */
#include "os32api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PIPE_OWNER_CHILD "/usr/bin/pipe_owner_test.bin"
#ifndef PIPE_OWNER_LENGTH
#define PIPE_OWNER_LENGTH 0U
#endif

int main(int argc, char **argv, KernelAPI *api)
{
    int id, fails = 0;
    u32 length;
    if (argc == 3 && strcmp(argv[1], "foreign") == 0) {
        id = atoi(argv[2]);
        fails = api->sys_pipe_get_buf(id) != (u8 *)0 ||
                api->sys_pipe_get_len(id) != 0;
    } else if (argc == 1) {
        char command[128];
        int kind = EXEC_KIND_NONE, code = -1, rc;
        id = api->sys_pipe_alloc();
        if (id < 0) return 1;
        length = PIPE_OWNER_LENGTH; /* Newly allocated: 0; host seed: nonzero. */
        if (length == 0)
            api->kprintf(ATTR_WHITE, "pipe_owner_test: 長さ 0: 本人/他 owner の区別は host と trusted 対照で\n");
        if (api->sys_pipe_get_buf(id) != (u8 *)0) fails++;
        if (api->sys_pipe_get_len(id) != length) fails++;
        snprintf(command, sizeof(command), "%s foreign %d", PIPE_OWNER_CHILD, id);
        rc = api->exec_run(command);
        if (api->exec_last_result(&kind, &code) != 0 || rc != 0 ||
            kind != EXEC_KIND_EXITED || code != 0) fails++;
        if (api->sys_pipe_get_len(id) != length) fails++;
        api->sys_pipe_free(id);
    } else {
        api->kprintf(ATTR_RED, "usage: pipe_owner_test [foreign <id>]\n");
        return 1;
    }
    api->kprintf(fails ? ATTR_RED : ATTR_GREEN, "pipe_owner_test: %s\n",
                 fails ? "FAIL" : "PASS");
    return fails ? 1 : 0;
}
