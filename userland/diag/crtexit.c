#include <stdio.h>
#include <stdlib.h>
#include "os32api.h"

static char buffer[BUFSIZ];
static void emit_b(void) { putchar('B'); }
static void emit_c(void) { putchar('C'); }

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argc; (void)argv; (void)api;
    setvbuf(stdout, buffer, _IOFBF, sizeof(buffer));
    putchar('A');
    atexit(emit_b);
    atexit(emit_c);
    return 37;
}
