#include <stdarg.h>

/* Leaf code copied into an allocator-owned page beyond REL24 reach. */
int probe_sum(int *value, ...)
{
    va_list ap;
    va_start(ap, value);
    int sum = *value;
    for (int i = 1; i <= 9; i++) {
        int argument = va_arg(ap, int);
        if (argument != i) return -1;
        sum += argument;
    }
    va_end(ap);
    return sum;
}
