/* この像にはフックの提供者が無い (build/sources/hooks.mk)。宣言の側のヘッダを読む。 */
#define OS32_HOOK_R1 0
#include "exec_r1.h"
int exec_r1_fixture_lease(struct addrspace *as, u32 role) { (void)as; (void)role; return 0; }
void exec_r1_fixture_resume(int id) { (void)id; }
void exec_r1_fixture_parent(int id) { (void)id; }
