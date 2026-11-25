#ifndef KDEBUG_GFINIT_BUDGET_INC
#define KDEBUG_GFINIT_BUDGET_INC

#include "gfinit_plan.h"

#define GF_REC_FIXED_ROWS 128U
#define GF_REC_NODE_ROWS 12U
#define GF_REC_ARG_ROWS 4U
static int gfinit_log_rows(const struct recover_plan *plan, u32 first, u32 *rows)
{
    u32 nodes = plan->node_count, args = plan->arg_count;
    if (first != RECOVER_NONE) {
        if (first >= plan->node_count) return -1;
        nodes = first;
        args = plan->nodes[first].arg_first;
    }
    unsigned long long total = GF_REC_FIXED_ROWS + (unsigned long long)nodes * GF_REC_NODE_ROWS +
        (unsigned long long)args * GF_REC_ARG_ROWS;
    /* A main marker contains a bounded runner, including its raw bus rows
     * and local/transaction snapshots; it is not one expanded operation. */
    if (total > ~0U) return -1;
    for (u32 i=0; plan->nodes && i<nodes; i++) if (plan->nodes[i].opcode==GF_INIT_RUNNER) {
        total += (unsigned long long)GF_INIT_MAINS * GF_INIT_SCHEDULE * GF_REC_NODE_ROWS;
        break;
    }
    if (total > ~0U) return -1;
    *rows = (u32)total;
    return 0;
}
#endif
