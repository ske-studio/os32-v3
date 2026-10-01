/* T2d d0a: registered stdout belongs to the parent AS, even in a child.
 * The same binary gives both processes the same BSS VA, with private backing.
 * No heap/SHM alias or guessed address is used. main must be the first function.
 */
#include "os32api.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PROBE_BYTES 64
#define PARENT_FILL 0xA5
#define CHILD_FILL 0x5A
#define CHILD_CHANGED 10
#define CHILD_WRITE_FAILED 11
#define CHILD_BAD_VA 12
#define CHILD_BAD_CPL 13
#define REPORT_ATTR 0x07
#define CHILD_PATH "/usr/bin/d0a_test.bin"

static volatile u8 probe[PROBE_BYTES];
static const char payload[] = "d0a child stdout\n";
static int child_main(int argc, char **argv, KernelAPI *api);

int main(int argc, char **argv, KernelAPI *api)
{
    unsigned short cs;
    char command[128];
    int i, rc, result_rc, kind = EXEC_KIND_NONE, code = -1;
    int parent_ok = 1, child_ok;
    u32 len;

    __asm__ volatile ("mov %%cs, %0" : "=r"(cs));
    if ((cs & 3) != 3) {
        api->kprintf(REPORT_ATTR, "d0a: setup=FAIL cpl=%u\n", cs & 3);
        return CHILD_BAD_CPL;
    }
    if (argc > 1 && strcmp(argv[1], "--child") == 0)
        return child_main(argc, argv, api);

    for (i = 0; i < PROBE_BYTES; i++) probe[i] = PARENT_FILL;
    snprintf(command, sizeof(command), "%s --child %lu", CHILD_PATH, (u32)probe);
    api->kprintf(REPORT_ATTR, "d0a: cpl=3 buffer_va=0x%lx\n", (u32)probe);
    rc = api->sys_redirect_fd_buf(1, (u8 *)probe, sizeof(probe), 0);
    if (rc < 0) {
        api->kprintf(REPORT_ATTR, "d0a: setup=FAIL redirect_rc=%d\n", rc);
        return 1;
    }
    rc = api->exec_run(command);
    result_rc = api->exec_last_result(&kind, &code);
    len = api->sys_redirect_get_buf_len(1);
    api->sys_reset_redirect(1);  /* report only after restoring stdout */
    if (len != sizeof(payload) - 1) parent_ok = 0;
    for (i = 0; i < PROBE_BYTES; i++) {
        u8 expected = (i < (int)sizeof(payload) - 1) ? (u8)payload[i] : PARENT_FILL;
        if (probe[i] != expected) parent_ok = 0;
    }
    child_ok = result_rc == 0 && kind == EXEC_KIND_EXITED && code == 0 && rc == 0;
    api->kprintf(REPORT_ATTR, "d0a: parent_buffer=%s\n", parent_ok ? "OK" : "MISSING");
    /* A fault/launch failure cannot tell us the child's final bytes. */
    api->kprintf(REPORT_ATTR, "d0a: child_value=%s\n", child_ok ? "OK" :
                 (result_rc == 0 && kind == EXEC_KIND_EXITED && code == CHILD_CHANGED && rc == code ?
                  "CHANGED" : "UNVERIFIED"));
    api->kprintf(REPORT_ATTR, "d0a: child_status kind=%d code=%d rc=%d result_rc=%d bytes=%lu\n",
                 kind, code, rc, result_rc, len);
    return parent_ok && child_ok ? 0 : 1;
}

static int child_main(int argc, char **argv, KernelAPI *api)
{
    int i, written, changed = 0;
    if (argc != 3 || strtoul(argv[2], NULL, 10) != (u32)probe)
        return CHILD_BAD_VA;
    for (i = 0; i < PROBE_BYTES; i++) probe[i] = CHILD_FILL;
    written = api->sys_write(1, payload, sizeof(payload) - 1);
    for (i = 0; i < PROBE_BYTES; i++) {
        if (probe[i] != CHILD_FILL) changed = 1;
    }
    if (written != (int)sizeof(payload) - 1) return CHILD_WRITE_FAILED;
    return changed ? CHILD_CHANGED : 0;
}
