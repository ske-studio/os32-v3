#ifndef VFS_STDIN_H
#define VFS_STDIN_H

#include "types.h"

/* Internal read progress; buf belongs to the caller, never the kernel stack. */
typedef struct VfsStdinRead {
    u8 *buf;
    u32 size;
    u32 done;
    int active;
} VfsStdinRead;

int vfs_stdin_continue(VfsStdinRead *state);

#endif
