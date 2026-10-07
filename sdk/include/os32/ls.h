/* CRT (syscalls.o) USER enumeration. Callback storage is valid only during the call.
 * A void callback has no early-stop return; callers may ignore later entries. */
#ifndef OS32_LIB_LS_H
#define OS32_LIB_LS_H
#include "os32api.h"
int os32_ls(const char *path, DirCallback cb, void *ctx);
#endif
