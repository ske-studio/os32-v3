#include "ring3_ls.h"
#include "paging.h"
#include "exec.h"
#include "redir_access.h"
#include "kstring.h"
#include "vfs.h"

struct ring3_ls_packet {
    u32 count;
    int result;
    u32 done;
    VfsDirEntry entries[RING3_LS_BATCH];
};

STATIC_ASSERT(__builtin_offsetof(struct ring3_ls_packet, result) == RING3_LS_RESULT_OFF, ls_result_offset);
STATIC_ASSERT(__builtin_offsetof(struct ring3_ls_packet, done) == RING3_LS_DONE_OFF, ls_done_offset);

STATIC_ASSERT(VFS_MAX_PATH == RING3_LS_PATH_SIZE, ls_path_size);
STATIC_ASSERT(RING3_LS_STACK_SIZE <= PAGE_SIZE, ls_stack_budget);
STATIC_ASSERT(sizeof(VfsDirEntry) == RING3_LS_ENTRY_SIZE, ls_entry_size);
STATIC_ASSERT(__builtin_offsetof(struct ring3_ls_packet, entries) ==
              RING3_LS_ENTRIES_OFF, ls_entries_offset);
STATIC_ASSERT(sizeof(struct ring3_ls_packet) == RING3_LS_PACKET_SIZE, ls_packet_size);

STATIC_ASSERT(sizeof(OS32_LsEntry) == sizeof(VfsDirEntry), abi_LsEntry_size);
STATIC_ASSERT(__builtin_offsetof(OS32_LsEntry, name) == __builtin_offsetof(VfsDirEntry, name), abi_LsEntry_name);
STATIC_ASSERT(__builtin_offsetof(OS32_LsEntry, size) == __builtin_offsetof(VfsDirEntry, size), abi_LsEntry_size_offset);
STATIC_ASSERT(__builtin_offsetof(OS32_LsEntry, type) == __builtin_offsetof(VfsDirEntry, type), abi_LsEntry_type);
STATIC_ASSERT(sizeof(OS32_LsPacket) == sizeof(struct ring3_ls_packet), abi_LsPacket_size);
STATIC_ASSERT(__builtin_offsetof(OS32_LsPacket, count) == __builtin_offsetof(struct ring3_ls_packet, count), abi_LsPacket_count);
STATIC_ASSERT(__builtin_offsetof(OS32_LsPacket, result) == __builtin_offsetof(struct ring3_ls_packet, result), abi_LsPacket_result);
STATIC_ASSERT(__builtin_offsetof(OS32_LsPacket, done) == __builtin_offsetof(struct ring3_ls_packet, done), abi_LsPacket_done);
STATIC_ASSERT(__builtin_offsetof(OS32_LsPacket, entries) == __builtin_offsetof(struct ring3_ls_packet, entries), abi_LsPacket_entries);

struct ls_collect {
    struct ring3_ls_packet packet;
};

static int ls_copy_path(const struct caller_access *caller, u32 va, char *path)
{
    /* NULL still means cwd. All other inputs require NUL inside the bound;
     * no synthesized terminator before entering the filesystem. */
    if (!va) { path[0] = 0; return 1; }
    return copy_caller_cstr(caller, (const char *)(uptr)va,
                            path, VFS_MAX_PATH);
}

static void ls_collect_entry(const VfsDirEntry *entry, void *opaque)
{
    struct ls_collect *c = opaque;
    if (c->packet.count == RING3_LS_BATCH) {
        c->packet.done = 0;
        return;
    }
    VfsDirEntry *out = &c->packet.entries[c->packet.count++];
    /* Do not expose backend stack padding or bytes past the name's NUL. */
    kstrncpy(out->name, entry->name, sizeof(out->name));
    out->size = entry->size;
    out->type = entry->type;
}

int ring3_ls_window(const char *user_path, u32 skip, OS32_LsPacket *out)
{
    struct caller_access caller;
    char path[VFS_MAX_PATH + 1];
    /* Non-reentrant: int80 runs cooperatively, and this collector invokes
     * only kernel code. USER callbacks run after the FS and copy have unwound.
     * Nested USER listings therefore start after this storage is released. */
    static struct ls_collect collect;

    if (!caller_access_get(&caller) || caller.origin != CALLER_USER) return OS32_ERR_INVAL;
    if (!ls_copy_path(&caller, (u32)(uptr)user_path, path) ||
        !check_caller_write_range(&caller, out, sizeof(collect.packet))) {
        ring3_fault_kill();
        return OS32_ERR_INVAL;
    }
    kmemset(&collect, 0, sizeof(collect));
    collect.packet.done = 1;
    collect.packet.result = vfs_ls_window(path, skip, RING3_LS_BATCH, ls_collect_entry, &collect);
    /* FS has unwound before touching USER memory or returning to the callback.
     * No FD, allocation, saved cursor or backend state survives this syscall.
     * A callback may be killed/parked/nested without a cleanup obligation. */
    if (!copy_to_caller(&caller, out, &collect.packet,
                        sizeof(collect.packet))) {
        ring3_fault_kill();
        return OS32_ERR_INVAL;
    }
    return 0;
}

int ring3_ls_dispatch(u32 user_esp)
{
    struct caller_access caller;
    u32 args[3];                 /* path, skip, output packet; no callback */
    if (user_esp > ~(u32)0 - sizeof(u32) ||
        !caller_access_get_user(&caller) ||
        !copy_caller_bytes(&caller, (const void *)(uptr)(user_esp + sizeof(u32)),
                           args, sizeof(args))) {
        ring3_fault_kill();
        return OS32_ERR_INVAL;
    }
    return ring3_ls_window((const char *)(uptr)args[0], args[1],
                          (OS32_LsPacket *)(uptr)args[2]);
}
