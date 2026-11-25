/* Two sequential child ASes, for both explicit free and exit cleanup. */
#include "os32api.h"
#include "rt/testresult.h"
#include <stdio.h>
#define CHILD_PATH "/usr/bin/shm_reuse_child.bin"
#define WRITE_PATTERN 0x39574853UL
static int run_child(KernelAPI *api, const char *mode, volatile u32 *report);
int main(int argc, char **argv, KernelAPI *api)
{
    volatile u32 *report = (volatile u32 *)api->sys_shm_alloc(1);
    int passed = 0, mode;
    (void)argc; (void)argv;
    if (report) {
        for (mode = 0; mode < 2; mode++) {
            int first, second;
            report[0] = report[1] = report[2] = report[3] = 0;
            first = run_child(api, mode ? "exit" : "free", report);
            if (first && report[0] && report[1] == (mode ? 1UL : 2UL)) passed++;
            second = run_child(api, "write", report);
            if (first && second && report[0] == report[2] && report[3] == WRITE_PATTERN)
                passed++;
            api->kprintf(ATTR_WHITE, "shm_reuse %s report=%lx first=%lx second=%lx lock=%lu write=%lx\n",
                         mode ? "exit" : "free", (u32)report, report[0], report[2], report[1], report[3]);
        }
        /* Keep report bytes for PM inspection; exec_exit releases ownership. */
    }
    return os32_test_summary(api, "shm_reuse_test", passed, 4);
}
static int run_child(KernelAPI *api, const char *mode, volatile u32 *report)
{
    char command[128];
    int rc, kind = EXEC_KIND_NONE, code = -1;
    snprintf(command, sizeof(command), "%s %s %lx", CHILD_PATH, mode, (u32)report);
    rc = api->exec_run(command);
    return api->exec_last_result(&kind, &code) == 0 && rc == 0 &&
           kind == EXEC_KIND_EXITED && code == 0;
}
