/* Actual dispatcher/access/WM functions; slot/CR3/IRQ/KAPI are boundaries.
 * Nonlocal lifetime coverage lives in the real exec R1 fixture. */
#define _GNU_SOURCE
#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <string.h>
#include "appslot.h"
#include "v86.h"
#define __KSTRING_H
void *kmemcpy(void *d, const void *s, u32 n) { return memcpy(d, s, n); }
#define IO_H
static unsigned int host_if = 1;
static unsigned int irq_save(void) { unsigned int f = host_if; host_if = 0; return f; }
static void irq_restore(unsigned int f) { assert(!host_if); host_if = f; }
#include "../../exec/redir_access.c"
static AppSlot slots[APP_SLOT_COUNT], *g_cur_app;
static struct addrspace spaces[APP_SLOT_COUNT];
static int cur = 2, owner = 2, user_call = 1;
static u32 cr3 = 0x2000, tick_count;
static int ring3_in_syscall, invoked, nesting;
volatile int ring3_wm_depth;
volatile u32 ring3_caller_reject_count, ring3_wm_depth_underflow;
static u32 *g_cur_frame;
static jmp_buf killed;
AppSlot *appslot_get(int id) { assert(!host_if); return id > 0 && id < APP_SLOT_COUNT && slots[id].state ? &slots[id] : NULL; }
int appslot_cur(void) { return cur; }
int res_owner_get(void) { return owner; }
int ring3_call_from_user(void) { return user_call; }
u32 paging_kernel_pd_phys(void) { return 0x1000; }
u32 paging_current_cr3(void) { return cr3; }
int as_va_to_pa(u32 pd, u32 va, u32 *pa) { (void)pd; *pa = va; return 0; }
int as_va_to_pa_read(u32 pd, u32 va, u32 *pa) { return as_va_to_pa(pd, va, pa); }
int as_access_page(const struct addrspace *as, u32 va, int write, u32 *pa) { return !(write ? as_va_to_pa(as->pd_phys, va, pa) : as_va_to_pa_read(as->pd_phys, va, pa)); }
static u32 host_kapi_table[KAPI_FUNC_COUNT + 2];
#define KAPI_ADDR host_kapi_table
#define RING3_USTACK_TOP MEM_APP_STACK_TOP
#define RING3_ARG_WINDOW 64u
const u16 kapi_argsize[KAPI_FUNC_COUNT] = {0};
const u16 kapi_argptr[KAPI_FUNC_COUNT] = {0};
static void ring3_abort_check(void) {}
static void ring3_gui_pump(void) {}
static int stop_park_calls;
static void exec_park_stop(u32 *frame) { (void)frame; stop_park_calls++; }
static int ring3_ptr_ok(u32 p) { (void)p; return 1; }
static void ring3_fault_kill(void) { longjmp(killed, 1); }
static u32 kapi_invoke(void *fn, const void *args, u32 n);
static int guest_ints;
void v86_int80(u32 *frame) {
    assert(frame[V86I_EFLAGS] & EFLAGS_VM);
    guest_ints++;
}
/* The runner inserts the unmodified ring3_syscall_dispatch definition here. */
#include "dispatcher.inc"
#include "wm.inc"
static u32 kapi_invoke(void *fn, const void *args, u32 n)
{
    struct caller_access a, b;
    CallerAccessFrame previous;
    (void)fn; (void)args; (void)n;
    invoked++;
    assert(ring3_in_syscall && caller_access_get(&a));
    assert(a.origin == CALLER_USER && a.app_id == cur && a.pd_phys == cr3);
    assert(a.as == &spaces[cur] && a.owner == spaces[cur].owner);
    assert(a.generation == spaces[cur].generation);
    if (!nesting) {
        u32 child[13] = {0};
        nesting = 1;
        cur = owner = 3; cr3 = 0x3000; g_cur_app = &slots[3];
        child[11] = MEM_APP_BAND_BASE;
        ring3_syscall_dispatch(child);
        assert(child[7] == 73 && ring3_in_syscall);
        cur = owner = 2; cr3 = 0x2000; g_cur_app = &slots[2];
        assert(caller_access_get(&b) && !memcmp(&a, &b, sizeof(a)));
        assert(caller_access_enter(&previous, CALLER_TRUSTED));
        assert(caller_access_get(&b) && b.origin == CALLER_TRUSTED);
        caller_access_leave(&previous);
        assert(caller_access_get(&b) && !memcmp(&a, &b, sizeof(a)));
        nesting = 0;
    }
    return 73;
}
static void select_parent(void) { cur = owner = 2; cr3 = 0x2000; g_cur_app = &slots[2]; }
int main(void)
{
    struct caller_access a, out;
    CallerAccessFrame previous, nested;
    for (int id = 2; id <= 3; id++) {
        slots[id].state = APP_STATE_RUNNING; slots[id].cpl3 = 1;
        slots[id].as = &spaces[id]; slots[id].stack_top = MEM_APP_BAND_BASE + PAGE_SIZE;
        spaces[id].owner = id + 10; spaces[id].generation = id + 20;
        spaces[id].pd_phys = id * PAGE_SIZE;
    }
    select_parent();
    /* VM=1 reaches int80 with 17 words, including real-mode segments.
     * Valid and invalid slots must both bypass caller setup, abort and KAPI. */
    for (int valid = 0; valid < 2; valid++) {
        u32 vm[17] = {0};
        vm[7] = valid ? 0 : ~0U;
        vm[V86I_EFLAGS] = EFLAGS_VM | EFLAGS_IOPL3;
        vm[V86I_ES] = 0xa000; vm[V86I_DS] = 0x8000;
        vm[V86I_FS] = 0x1234; vm[V86I_GS] = 0x5678;
        u32 saved[17]; memcpy(saved, vm, sizeof(vm));
        if (!setjmp(killed)) ring3_syscall_dispatch(vm); else assert(0);
        assert(guest_ints == valid + 1 && !invoked && !stop_park_calls);
        assert(!caller_access_get(&out) && !g_cur_frame && !ring3_in_syscall);
        assert(!memcmp(saved, vm, sizeof(vm)));
    }
    assert(!caller_access_get(&out));
    for (unsigned int f = 0; f <= 1; f++) {
        host_if = f;
        assert(caller_access_enter(&previous, CALLER_USER));
        assert(caller_access_get(&a));
        assert(host_if == f && cr3 == 0x2000);
        out = a;
        cr3 = paging_kernel_pd_phys();
        out.app_id = -99;
        struct caller_access sentinel = out;
        assert(!caller_access_get(&out) && !memcmp(&out, &sentinel, sizeof(out)));
        assert(!caller_access_enter(&nested, CALLER_USER));
        cr3 = 0x2000;
        assert(caller_access_get(&out));
        cur = 3; assert(!caller_access_get(&out)); cur = 2;
        owner = 3; assert(!caller_access_get(&out));
        assert(!caller_access_enter(&nested, CALLER_USER)); owner = 2;
        spaces[2].generation++; assert(!caller_access_get(&out)); spaces[2].generation--;
        spaces[2].owner++; assert(!caller_access_get(&out)); spaces[2].owner--;
        spaces[2].pd_phys++; assert(!caller_access_get(&out)); spaces[2].pd_phys--;
        struct addrspace same_fields = spaces[2];
        slots[2].as = &same_fields; assert(!caller_access_get(&out)); slots[2].as = &spaces[2];
        slots[2].cpl3 = 0; assert(!caller_access_get(&out)); slots[2].cpl3 = 1;
        slots[2].state = APP_STATE_ABORT_PENDING; assert(!caller_access_get(&out));
        slots[2].state = APP_STATE_FAULT_PENDING; assert(!caller_access_get(&out));
        slots[2].state = APP_STATE_FREE; assert(!caller_access_get(&out));
        slots[2].state = APP_STATE_RUNNING;
        assert(!caller_access_enter(&nested, (enum caller_origin)99));
        assert(caller_access_get(&out) && !memcmp(&a, &out, sizeof(a)));
        /* Explicit trusted origin works even in a user PD; master alone never
         * converted the USER descriptor above into trusted. */
        assert(caller_access_enter(&nested, CALLER_TRUSTED));
        assert(caller_access_get(&out) && out.origin == CALLER_TRUSTED);
        assert(out.as == NULL && out.generation == 0);
        caller_access_leave(&nested);
        assert(caller_access_get(&out) && !memcmp(&a, &out, sizeof(a)));
        caller_access_leave(&previous);
        assert(!caller_access_get(&out) && host_if == f && cr3 == 0x2000);
        u32 frame[13] = {0}; frame[11] = MEM_APP_BAND_BASE;
        ring3_syscall_dispatch(frame);
        assert(frame[7] == 73 && !ring3_in_syscall && !g_cur_frame);
        assert(!caller_access_get(&out) && host_if == f && cr3 == 0x2000);
        assert(caller_access_enter(&previous, CALLER_TRUSTED));
        assert(caller_access_get(&a));
        frame[7] = 0;
        ring3_syscall_dispatch(frame);
        assert(caller_access_get(&out) && !memcmp(&a, &out, sizeof(a)));
        caller_access_leave(&previous);
        /* Nested explicit WM scopes preserve the USER descriptor and IF/PD. */
        assert(caller_access_enter(&previous, CALLER_USER));
        assert(caller_access_get_user(&a));
        ring3_wm_enter(); ring3_wm_enter();
        assert(caller_access_get(&out) && out.origin == CALLER_TRUSTED);
        assert(caller_access_get_user(&out) && !memcmp(&a, &out, sizeof(a)));
        ring3_wm_leave();
        assert(caller_access_get(&out) && out.origin == CALLER_TRUSTED);
        ring3_wm_leave();
        assert(caller_access_get(&out) && !memcmp(&a, &out, sizeof(a)));
        caller_access_invalidate();
        assert(!caller_access_get_user(&out) && !caller_access_get(&out));
        caller_access_leave(&previous);
        assert(caller_access_enter(&previous, CALLER_TRUSTED));
        assert(!caller_access_get_user(&out));
        caller_access_leave(&previous);
        int before = invoked;
        u32 rejects = ring3_caller_reject_count;
        cr3 = paging_kernel_pd_phys();
        if (!setjmp(killed)) { ring3_syscall_dispatch(frame); assert(0); }
        assert(ring3_caller_reject_count == rejects + 1);
        assert(invoked == before && !caller_access_get(&out) && host_if == f);
        /* The kill fixture does not emulate d2 landing; reset only test state. */
        select_parent(); g_cur_frame = NULL;
    }
    assert(stop_park_calls == invoked);
    puts("caller d2: actual dispatcher, USER/TRUSTED/WM/nesting/rejection/IF PASS");
    return 0;
}
