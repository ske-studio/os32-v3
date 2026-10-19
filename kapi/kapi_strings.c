#include "kapi_strings.h"
#include "redir_access.h"
#include "os32_kapi_slots.h"
#include "kmalloc.h"
#include "appslot.h"
#include "vfs.h"
#include "launch.h"

extern u32 kapi_invoke(void *target, const void *args, u32 nbytes);

struct string_input {
    u16 mask, nullable;
    u32 cap;
};
/* Only public USER entries pass here; internal CPL0 callers keep their direct
 * wrappers. All inputs in a row are copied before invoking even a validator. */
static const struct string_input inputs[KAPI_FUNC_COUNT] = {
    [KAPI_SLOT_SERIAL_PUTS] = {1, 0, KAPI_STRING_CAP},
    [KAPI_SLOT_SHELL_PRINT_UTF8] = {1, 0, KAPI_STRING_CAP},
    [KAPI_SLOT_FM_PLAY_MML] = {1, 0, KAPI_STRING_CAP},
    [KAPI_SLOT_SND_BGM_PLAY] = {1, 0, KAPI_STRING_CAP},
    [KAPI_SLOT_EXEC_RUN] = {1, 0, KAPI_STRING_CAP},
    [KAPI_SLOT_EXEC_START] = {1, 1, KAPI_STRING_CAP},
    [KAPI_SLOT_LAUNCH_REQ] = {1, 1, LAUNCH_CMDLINE_MAX},
    [KAPI_SLOT_KCG_LOAD_FONT] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_LOOP_ATTACH] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_DEV_BLK_READ] = {1, 0, VFS_MAX_PATH},
    [KAPI_SLOT_DEV_BLK_WRITE] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_SWITCH_SHELL] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_MOUNT] = {7, 7, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_UMOUNT] = {1, 0, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_UMOUNT_CHECKED] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_IS_MOUNTED] = {1, 0, VFS_MAX_PATH},
    [KAPI_SLOT_VFS_DEVNAME] = {1, 0, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_UNLINK] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_RENAME] = {3, 3, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_MKDIR] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_RMDIR] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_CHDIR] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_OPEN] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_STAT] = {1, 1, VFS_MAX_PATH},
    [KAPI_SLOT_SYS_REDIRECT_FD] = {2, 2, VFS_MAX_PATH},
    [KAPI_SLOT_PATH_SET_DRIVE] = {1, 0, VFS_MAX_PATH},
    [KAPI_SLOT_PATH_SET_CWD] = {1, 0, VFS_MAX_PATH},
    [KAPI_SLOT_PATH_PARSE] = {1, 0, VFS_MAX_PATH},
};

/* One active syscall per app (cooperative). Nested exec uses a different slot;
 * abandoned kernel frames never own the only reference to a KHEAP buffer. */
static char *staging[APP_SLOT_COUNT][KAPI_STRING_ARGS];

void kapi_strings_cleanup_owned(int app_id)
{
    if (app_id < APP_ID_MIN || app_id > APP_ID_MAX) return;
    for (u32 i = 0; i < KAPI_STRING_ARGS; i++) {
        char *p = staging[app_id][i];
        staging[app_id][i] = 0;
        if (p) kfree(p);
    }
}

int kapi_strings_call(u32 slot, void *target, const void *user_args,
                      u32 nbytes, u16 ptrmask, u32 *result)
{
    const struct string_input *input;
    struct caller_access caller;
    u32 args[KAPI_STRING_WORDS], count = 0;
    int ok = -1;
    /* Constant-time absence check also keeps unrelated drawing/poll syscalls
     * out of the string-copy path. */
    if (slot >= KAPI_FUNC_COUNT || !inputs[slot].mask) return 0;
    input = &inputs[slot];
    if (!caller_access_get(&caller) || caller.origin != CALLER_USER ||
        caller.app_id < APP_ID_MIN || caller.app_id > APP_ID_MAX ||
        nbytes > sizeof(args) || nbytes % sizeof(u32) ||
        !copy_caller_bytes(&caller, user_args, args, nbytes)) return -1;
    /* Preserve the early pointer gate for the remaining buffer arguments.
     * String pointers instead require the entire NUL input, through B1. */
    for (u32 i = 0; i < nbytes / sizeof(u32); i++) {
        u16 bit = (u16)(1u << i);
        if (!(input->mask & bit)) {
            if ((ptrmask & bit) && args[i] &&
                !redir_access_check(&caller, args[i], 1, 0)) goto out;
            continue;
        }
        if (!args[i] && (input->nullable & bit)) continue;
        if (count == KAPI_STRING_ARGS) goto out;
        char *p = kmalloc(input->cap);
        staging[caller.app_id][count++] = p;
        if (!p || !copy_caller_cstr(&caller, (const char *)(uptr)args[i],
                                     p, input->cap)) goto out;
        args[i] = (u32)(uptr)p;
    }
    *result = kapi_invoke(target, args, nbytes);
    ok = 1;
out:
    kapi_strings_cleanup_owned(caller.app_id);
    return ok;
}
