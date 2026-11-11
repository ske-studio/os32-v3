#ifndef OS32_EXEC_R1_H
#define OS32_EXEC_R1_H
#include "types.h"
struct addrspace;
int exec_r1_fixture_lease(struct addrspace *as, u32 role);
void exec_r1_fixture_resume(int id);
void exec_r1_fixture_parent(int id);
#endif
