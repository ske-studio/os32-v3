#include "boot_token.h"
static u8 token[16];
static int ready;
void boot_token_init(void)
{
    if (!ready) {
        platform_boot_token(token);
        ready = 1;
    }
}
void boot_token_get(u8 out[16])
{
    boot_token_init();
    for (unsigned int i = 0; i < 16; ++i) out[i] = token[i];
}
