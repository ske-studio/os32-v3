#ifndef OS32_ARCH_EXEC_SELFTEST_H
#define OS32_ARCH_EXEC_SELFTEST_H

#include "kselftest.h"

static inline void exec_selftest_run_audit(const char *tag)
{
    kselftest_run_audit(tag);
}

#endif
