#ifndef OS32_EXEC_R1_H
#define OS32_EXEC_R1_H
#include "exec_hooks.h"
#if OS32_HOOK_R1
#include "r1_fixture.h"
#ifdef OS32_R1_FIXTURE
static inline int exec_r1_fixture_lease(struct addrspace *as, u32 role) { return r1_fixture_lease(as, role); }
static inline void exec_r1_fixture_resume(int id) { r1_fixture_resume(id); }
static inline void exec_r1_fixture_parent(int id) { r1_fixture_parent(id); }
#endif
#else
#include "types.h"
struct addrspace;
int exec_r1_fixture_lease(struct addrspace *as, u32 role);
void exec_r1_fixture_resume(int id);
void exec_r1_fixture_parent(int id);
#endif
#endif
