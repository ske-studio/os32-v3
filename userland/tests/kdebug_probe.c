#include "os32api.h"
#include <stdio.h>

int main(int argc, char **argv, KernelAPI *api)
{
    MemStat st;
    int rc;
    (void)argc; (void)argv;
    rc = api->mem_stat(0, &st, sizeof(st));
    if (rc != sizeof(st)) {
        printf("kdebug_probe: mem_stat failed (rc=%d)\n", rc);
        printf("kdebug_probe: FAIL 0/1\n");
        return 1;
    }
    printf("kdebug_state=%lu kdebug_pair_crc=%08lx kdebug_pages=%lu "
           "kdebug_owner_pages=%lu kdebug_open_count=%lu\n",
           st.kdebug_state, st.kdebug_pair_crc, st.kdebug_pages,
           st.kdebug_owner_pages, st.kdebug_open_count);
    /* guest_tests requires a summary for a successful observation. */
    printf("kdebug_probe: PASS 1/1\n");
    return 0;
}
