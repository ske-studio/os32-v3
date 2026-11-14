#include "boot_token.h"
#include <string.h>
static int calls;
void platform_boot_token(u8 out[16])
{
    ++calls;
    for (unsigned i = 0; i < 16; ++i) out[i] = (u8)(i + 1);
}
void wrap_boot_token_get(u8 *out);
int main(void)
{
    u8 a[18] = {0}, b[18] = {0};
    boot_token_init();
    boot_token_get(a + 1);
    wrap_boot_token_get(b + 1);
    if (calls != 1 || memcmp(a, b, sizeof(a))) return 1;
    for (unsigned i = 0; i < 16; ++i) if (b[i + 1] != i + 1) return 2;
    return 0;
}
