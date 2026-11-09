/* Internal diagnostic stack provider; zero means unavailable.
 * sqlite3.c restores its amalgamation line numbers after this optional include:
 * SQLite embeds __LINE__ in error diagnostics even when TRACE is disabled.
 */
#ifndef OS32_DIAG_STACK_H
#define OS32_DIAG_STACK_H
#include <arch_diag_stack.h>
#endif
