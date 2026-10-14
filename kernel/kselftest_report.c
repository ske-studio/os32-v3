#include "kselftest.h"
#include "kprintf.h"

/* Stable kernel.map symbols, including when no debug module is loaded. */
int kselftest_pass;
int kselftest_fail;

void kselftest_count(int pass)
{
    if (pass) kselftest_pass++;
    else kselftest_fail++;
}

void kdi_kselftest_count(int pass) { kselftest_count(pass); }
void kdi_kselftest_reset(void) { kselftest_pass = kselftest_fail = 0; }
u32 kdi_get_kselftest_pass(void) { return kselftest_pass; }
u32 kdi_get_kselftest_fail(void) { return kselftest_fail; }

void kselftest_skipped(void)
{
    kdi_kselftest_reset();
    kprintf(0xA1, "[selftest] skipped (KDEBUG=0)\n");
}
