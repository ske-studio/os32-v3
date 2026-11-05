/* Resident facade: model a loaded module, then run the real owner-exit hook. */
#include "fep_module.h"
#include "fep_boundary_runtime.h"
#include "fep_module_source.inc"
int services_public = 1;
int input_getkey_wait(int interruptible) { (void)interruptible; return -1; }
int input_getkey(void) { return -1; }
int input_trygetkey(void) { return -1; }
int input_trygetchar(void) { return -1; }
int input_getkey_kapi(void) { return -1; }
int con_sink_is_enabled(void) { return 0; }
void ring3_abort_check(void) {}
static int owner = 2, close_calls, reopen_calls, exit_calls, body_rc;
int res_owner_get(void) { return owner; }
const u32 *module_exports(struct module_handle handle)
{ static const u32 present = 1; (void)handle; return &present; }
static int body_suspend(int on)
{ if (on) close_calls++; else reopen_calls++; return body_rc; }
static void body_exit(int who) { (void)who; exit_calls++; }
static void require(int cond, const char *name)
{ __asm__ volatile("" : "+r"(cond)); if (!cond) { printf("FAIL %s\n", name); exit(1); } }
int main(void)
{
    candidate.dict_suspend = body_suspend;
    candidate.owner_exit = body_exit;
    require(!ime_dict_suspend(1) && close_calls == 1, "resident dispatch");
    ime_owner_exit(3);
    require(!reopen_calls && exit_calls == 1, "other owner untouched");
    ime_owner_exit(2);
    require(reopen_calls == 1 && exit_calls == 2, "STOP reopens through owner cleanup");
    ime_owner_exit(2);
    require(reopen_calls == 1, "owner cleanup once");
    require(!ime_dict_suspend(1) && !ime_dict_suspend(0), "normal restore");
    ime_owner_exit(2);
    require(reopen_calls == 2, "normal restore clears owner");
    body_rc = -9;
    require(ime_dict_suspend(1) == -9, "resident failure preserved");
    ime_owner_exit(2);
    require(reopen_calls == 2, "failed suspend not owned");
    body_rc = 0;
    require(!ime_dict_suspend(1), "prepare reopen failure");
    body_rc = -10;
    require(ime_dict_suspend(0) == -10, "reopen failure preserved");
    ime_owner_exit(2);
    require(reopen_calls == 3, "failed reopen stays disabled without cleanup retry");
    puts("GREEN FEP owner cleanup");
    return 0;
}
