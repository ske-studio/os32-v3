/* Included only in the acceptance copy of exec.c, to observe its private
 * current-app pointer. Entry arguments follow arch/ppc/exec_entry.c. */
#include "ppc_frame.h"
#include "ppc_oea.h"

enum shell_point { FIRST_PARENT, FIRST_CHILD, CHILD_RETURN, CHILD_FAULT,
                   PARENT_FAULT, NEXT_PARENT, POINT_COUNT };
struct shell_observation {
    u32 tick, reader, child_generation;
    u32 generation, id, user, depth, owner, current_app, address_space, api, aux;
};
volatile struct shell_observation shell_flow[POINT_COUNT];

static u32 current_msr(void)
{
    u32 value;
    __asm__ volatile("mfmsr %0" : "=r"(value));
    return value;
}

static void observe_flow(enum shell_point point, u32 msr)
{
    volatile struct shell_observation *record = &shell_flow[point];
    unsigned flags = irq_save();
    record->tick = tick_count;
    record->reader = con_sink_reader_get();
    record->child_generation = g_cur_app && g_cur_app->as ? g_cur_app->as->generation : 0;
    record->generation = g_shell_service.generation;
    record->id = exec_nest_level ? appslot_cur() : 0;
    record->user = !!(msr & PPC_MSR_PR);
    record->depth = exec_nest_level;
    record->owner = res_owner_get();
    record->current_app = !g_cur_app ? 0 : g_cur_app == appslot_at(2) ? 2 : ~0u;
    irq_restore(flags);
}

void __real_arch_entry_enter(struct arch_entry_context *, u32);
void __wrap_arch_entry_enter(struct arch_entry_context *entry, u32 root)
{
    enum shell_point point = root ? FIRST_CHILD :
        shell_flow[FIRST_PARENT].generation ? NEXT_PARENT : FIRST_PARENT;
    if (!shell_flow[point].generation) {
        u32 words[6];
        __builtin_memcpy(words, entry->opaque, sizeof(words));
        observe_flow(point, current_msr());
        shell_flow[point].address_space = root;
        shell_flow[point].api = words[4];
        shell_flow[point].aux = words[5];
    }
    __real_arch_entry_enter(entry, root);
}

void __real_ppc_trap_dispatch(struct ppc_frame *);
void __wrap_ppc_trap_dispatch(struct ppc_frame *frame)
{
    /* An interrupt between commit and rfi still has a supervisor SRR1. */
    if ((frame->srr1 & PPC_MSR_PR) && !shell_flow[FIRST_CHILD].user)
        observe_flow(FIRST_CHILD, frame->srr1);
    __real_ppc_trap_dispatch(frame);
}

int __real_exec_run(const char *);
int __wrap_exec_run(const char *command)
{
    int rc = __real_exec_run(command);
    if (exec_nest_level == 1)
        observe_flow(rc == EXEC_ERR_FAULT ? CHILD_FAULT : CHILD_RETURN, current_msr());
    else if (!exec_nest_level && rc == EXEC_ERR_FAULT)
        observe_flow(PARENT_FAULT, current_msr());
    return rc;
}
