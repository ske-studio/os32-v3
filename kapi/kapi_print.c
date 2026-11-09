#include "kapi_print.h"
#include "kprintf.h"
#include "redir_access.h"
#include "kmalloc.h"

int kapi_print_call(struct kapi_arg_source *src)
{
    struct kapi_values fixed;
    u32 args[KAPI_PRINT_ARG_LIMIT];
    char *strings[KAPI_PRINT_ARG_LIMIT] = {0};
    char format[KAPI_PRINT_FORMAT_CAP];
    const char *p;
    u32 count = 0;
    int ok = 0;

    if (!arch_kapi_read_block(src, 0, KAPI_PRINT_FIXED_WORDS, &fixed) ||
        !copy_caller_cstr(&src->caller, (const char *)(uptr)fixed.value[1],
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
            !arch_kapi_va_next(src,
                (spec == 's' || spec == 'p') ? KAPI_VALUE_POINTER : KAPI_VALUE_INTEGER,
                &args[count])) goto out;
        /* NULL %s keeps the renderer's existing "(null)" semantics. */
        if (spec == 's' && args[count]) {
            strings[count] = kmalloc(KAPI_PRINT_STRING_CAP);
            if (!strings[count] ||
                !copy_caller_cstr(&src->caller, (const char *)(uptr)args[count],
                                  strings[count], KAPI_PRINT_STRING_CAP)) goto out;
            args[count] = (u32)(uptr)strings[count];
        }
        count++;
    }
    kvprintf((u8)fixed.value[0], format, args);
    ok = 1;
out:
    for (u32 i = 0; i < KAPI_PRINT_ARG_LIMIT; i++)
        if (strings[i]) kfree(strings[i]);
    return ok;
}
