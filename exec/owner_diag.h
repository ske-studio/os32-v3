#ifndef OS32_OWNER_DIAG_H
#define OS32_OWNER_DIAG_H
/* Only ordinary cooperative context may use the polled serial sink. */
static void __attribute__((cold)) exec_owner_diag(AppSlot *a, int exiting, u32 pages, u32 leftover)
{
    const char *label = " id=\0 owner=\0 gen=\0 pages=\0 leftover=\0 irq=\0 exc=";
    u32 values[] = {0, a->as->owner, a->as->generation, pages, leftover,
                    kctx_irq_depth, kctx_exc_depth};
    if (kctx_irq_depth || kctx_exc_depth) return;
    for (int id = APP_ID_MIN; id <= APP_ID_MAX; id++)
        if (appslot_at(id) == a) values[0] = id;
    serial_puts_polled(exiting ? "OS32: owner-exit" : "OS32: owner-start");
    for (u32 i = 0; i < (exiting ? 7U : 3U); i++) {
        char buf[11], *p = buf + sizeof(buf) - 1;
        u32 n = values[i];
        *p = 0;
        do { *--p = '0' + n % 10; n /= 10; } while (n);
        serial_puts_polled(label);
        while (*label++) {}
        serial_puts_polled(p);
    }
    serial_puts_polled("\r\n");
}
#endif
