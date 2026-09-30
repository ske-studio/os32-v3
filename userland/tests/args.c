#include "os32api.h"

void main(int argc, char **argv, KernelAPI *api)
{
    int i;
    
    api->kprintf(0x0E, "%s", "test_args: argc/argv test\n");
    
    api->kprintf(0x07, "argc = %d\n", argc);
    
    for (i = 0; i < argc; i++) {
        api->kprintf(0x0A, "argv[%d] = '%s'\n", i, argv[i] ? argv[i] : "(null)");
    }
    
    if (argv[argc] == (void *)0) {
        api->kprintf(0x02, "argv[argc] is NULL.\n");
    } else {
        api->kprintf(0x4F, "ERROR: argv[argc] is NOT NULL.\n");
    }
}
