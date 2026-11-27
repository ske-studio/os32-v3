/* Built only by kernel-tick-probe; uses the existing private kdebug call. */
#include "os32api.h"
#include "../../sys/kern/time/tick_probe.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv, KernelAPI *api)
{
    struct tp_request q = {0};
    if (argc == 2) {
        if (!strcmp(argv[1], "start")) q.action = 1;
        if (!strcmp(argv[1], "stop")) q.action = 2;
        if (!strcmp(argv[1], "idle")) q.action = TP_S1_IDLE;
        if (!strcmp(argv[1], "wait")) q.action = TP_S1_WAIT;
        if (!strcmp(argv[1], "due")) q.action = TP_S1_DUE;
        if (!strcmp(argv[1], "fixture")) q.action = TP_S1_FIXTURE;
        if (!strcmp(argv[1], "show")) q.action = 3;
    }
    if (argc == 3 && !strcmp(argv[1], "irq10diag") && !strcmp(argv[2], "show"))
        q.action = TP_IRQ10_SHOW;
    if (argc == 3 && !strcmp(argv[1], "tickack")) {
        if (!strcmp(argv[2], "on")) q.action = TP_TICKACK_ON;
        if (!strcmp(argv[2], "off")) q.action = TP_TICKACK_OFF;
    }
    if (argc == 3 && !strcmp(argv[1], "pcmirq")) {
        if (!strcmp(argv[2], "10")) q.action = TP_PCMIRQ_10;
        if (!strcmp(argv[2], "12")) q.action = TP_PCMIRQ_12;
        if (!strcmp(argv[2], "control")) q.action = TP_PCMIRQ_CONTROL;
    }
    if (!q.action) { printf("tickprobe start|idle|wait|due|fixture|stop|show|irq10diag show|tickack on|tickack off|pcmirq 10|pcmirq 12|pcmirq control\n"); return 1; }
    if (api->version < 78 || !api->kdebug_call) return 1;
    int rc = api->kdebug_call(TP_OP, &q, sizeof(q));
    if (rc) printf("tickprobe rc=%d (diagnostic image and KDEBUG=1 required)\n", rc);
    return rc ? 1 : 0;
}
