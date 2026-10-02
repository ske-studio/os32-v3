/* Host tool input only: offsets are emitted by the SAME ILP32 compiler.
 * No kernel code, exported API, or diagnostic write endpoint is added. */
#include <stddef.h>
#include "appslot.h"
#include "shm.h"
#include "pgalloc.h"
#define EMIT(name, value) __asm__ volatile(".globl h3_" #name "\n.set h3_" #name ", %c0" : : "i"(value))
void h3_layout(void)
{
    EMIT(slot_size, sizeof(AppSlot));
    EMIT(slot_state, offsetof(AppSlot, state));
    EMIT(slot_as, offsetof(AppSlot, as));
    EMIT(slot_in_wait, offsetof(AppSlot, in_op_wait));
    EMIT(slot_abort, offsetof(AppSlot, abort_req));
    EMIT(slot_tick, offsetof(AppSlot, last_kernel_tick));
    EMIT(running, APP_STATE_RUNNING);
    EMIT(slot_wait, offsetof(AppSlot, parked_from_wait));
    EMIT(as_owner, offsetof(struct addrspace, owner));
    EMIT(as_generation, offsetof(struct addrspace, generation));
    EMIT(ledger_size, sizeof(struct ledger_owner));
    EMIT(ledger_kind, offsetof(struct ledger_owner, kind));
    EMIT(ledger_id, offsetof(struct ledger_owner, id));
    EMIT(ledger_pages, offsetof(struct ledger_owner, pages));
    EMIT(ledger_as, LEDGER_KIND_AS);
    EMIT(ledger_count, LEDGER_MAX_OWNERS);
    EMIT(block_size, SHM_BLOCK_SIZE);
    EMIT(block_count, MEM_SHM_GUI_OFFSET / SHM_BLOCK_SIZE);
    EMIT(shm_delta, KHEAP_SIZE + MEM_KAPI_SIZE + MEM_GUARD_SIZE);
    EMIT(app_min, APP_ID_MIN);
    EMIT(app_max, APP_ID_MAX);
    EMIT(parked, APP_STATE_PARKED);
    EMIT(free, APP_STATE_FREE);
}
