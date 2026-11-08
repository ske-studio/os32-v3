/* Built only by kernel-tick-probe; uses the existing private kdebug call. */
#include "os32api.h"
#include "../../kernel/time/tick_probe.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv, KernelAPI *api)
{
    struct tp_request q = {0};
    if (argc == 2) {
        if (!strcmp(argv[1], "start")) q.action = 1;
        if (!strcmp(argv[1], "stop")) q.action = 2;
        if (!strcmp(argv[1], "show")) q.action = 3;
    }
    if (!q.action) { printf("tickprobe start|stop|show\n"); return 1; }
    if (api->version < 78 || !api->kdebug_call) return 1;
    int rc = api->kdebug_call(TP_OP, &q, sizeof(q));
    if (rc) printf("tickprobe rc=%d (diagnostic image and KDEBUG=1 required)\n", rc);
    return rc ? 1 : 0;
}
