#ifndef OS32_KAPI_PRINT_H
#define OS32_KAPI_PRINT_H

/* Private staging bounds, including NUL. No public ABI/layout change.
 * Argument count is checked against the existing int80 64-byte window. */
#define KAPI_PRINT_ARG_LIMIT 14u
#define KAPI_PRINT_FORMAT_CAP 1024u
#define KAPI_PRINT_STRING_CAP 4096u

/* Current USER caller only. Refusal emits nothing and releases all staging;
 * the dispatcher kills the caller after this returns, outside allocation. */
int kapi_print_call(const void *user_args);

#endif
