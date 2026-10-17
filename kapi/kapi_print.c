#include "kapi_print.h"
#include "kprintf.h"
#include "redir_access.h"
#include "kmalloc.h"

int kapi_print_call(const void *user_args)
{
    struct caller_access caller;
    u32 fixed[2], args[KAPI_PRINT_ARG_LIMIT];
    char *strings[KAPI_PRINT_ARG_LIMIT] = {0};
    char format[KAPI_PRINT_FORMAT_CAP];
    const char *p;
    u32 count = 0, va = (u32)(uptr)user_args;
    int ok = 0;

    if (!caller_access_get(&caller) || caller.origin != CALLER_USER ||
        !copy_caller_bytes(&caller, user_args, fixed, sizeof(fixed)) ||
        !copy_caller_cstr(&caller, (const char *)(uptr)fixed[1],
                          format, sizeof(format))) return 0;

    /* Parse the staged format before any console/UTF-8/redirect callback.
     * Copy only words the renderer consumes; do not require all 64 bytes. */
    p = format;
    while (*p) {
        if (*p++ != '%' || !*p) continue;
        int width, left;
        char pad;
        char spec = kprintf_spec(&p, &width, &left, &pad);
        if (!spec) break;
        p++;
        if (spec == '%') continue;
        if (count == KAPI_PRINT_ARG_LIMIT ||
            va > ~(u32)0 - sizeof(fixed) - count * sizeof(u32) ||
            !copy_caller_bytes(&caller,
                (const void *)(uptr)(va + sizeof(fixed) + count * sizeof(u32)),
                &args[count], sizeof(u32))) goto out;
        /* NULL %s keeps the renderer's existing "(null)" semantics. */
        if (spec == 's' && args[count]) {
            strings[count] = kmalloc(KAPI_PRINT_STRING_CAP);
            if (!strings[count] ||
                !copy_caller_cstr(&caller, (const char *)(uptr)args[count],
                                  strings[count], KAPI_PRINT_STRING_CAP)) goto out;
            args[count] = (u32)(uptr)strings[count];
        }
        count++;
    }
    kvprintf((u8)fixed[0], format, args);
    ok = 1;
out:
    for (u32 i = 0; i < KAPI_PRINT_ARG_LIMIT; i++)
        if (strings[i]) kfree(strings[i]);
    return ok;
}
