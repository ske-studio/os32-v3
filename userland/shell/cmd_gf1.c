/* Shared CUI/GUI terminal entry for the foreground layer-1 observations. */
#include "shell.h"
#include "../../kdebug/gf_foundation_wire.h"

#define GF1_KAPI_MIN 78U /* kdebug_call */

int shell_cmd_gf1(int argc, char **argv)
{
    struct gf1_request q = {0};
    if (argc != 2) goto usage;
    if (str_eq(argv[1], "status")) q.test = GF1_STATUS;
    else if (str_eq(argv[1], "instmem")) q.test = GF1_INSTMEM;
    else if (str_eq(argv[1], "vblank")) q.test = GF1_VBLANK;
    else if (str_eq(argv[1], "cycle")) q.test = GF1_CYCLE;
    else goto usage;
    if (g_api->version < GF1_KAPI_MIN || !g_api->kdebug_call) {
        g_api->kprintf(ATTR_RED, "gf1: rejected rc=%d kdebug unavailable\n", OS32_ERR_NOSYS);
        return 1;
    }
    q.size = sizeof(q);
    int rc = g_api->kdebug_call(GF_OP_FOUNDATION, &q, sizeof(q));
    return rc || q.rc ? 1 : 0;
usage:
    g_api->kprintf(ATTR_WHITE, "Usage: gf1 status|instmem|vblank|cycle\n");
    return 2;
}
