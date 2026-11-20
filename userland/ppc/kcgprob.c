#include "os32api.h"
#include <string.h>

/* JF-Dot-Shinonome16 KCG1 cells for A and JIS 3021 (亜). */
static const u8 ank[16] = {
    0x00, 0x10, 0x10, 0x28, 0x28, 0x28, 0x44, 0x44,
    0x44, 0x7c, 0x82, 0x82, 0x82, 0x82, 0x00, 0x00,
};
static const u8 kanji[32] = {
    0x00, 0x00, 0x3f, 0xff, 0x02, 0x20, 0x02, 0x20,
    0x02, 0x20, 0x1f, 0xfc, 0x12, 0x24, 0x12, 0x24,
    0x12, 0x24, 0x12, 0x24, 0x1f, 0xfc, 0x02, 0x20,
    0x02, 0x20, 0x02, 0x20, 0x7f, 0xff, 0x00, 0x00,
};

static int glyphs(KernelAPI *api, int missing)
{
    static const u8 blank[32];
    u8 buf[32];
    memset(buf, 0xa5, sizeof(buf));
    api->kcg_read_ank(0x41, buf);
    if (memcmp(buf, missing ? blank : ank, sizeof(ank))) return 1;
    memset(buf, 0xa5, sizeof(buf));
    api->kcg_read_kanji(0x3021, buf);
    return memcmp(buf, missing ? blank : kanji, sizeof(kanji)) != 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    int missing = argc == 2 && !strcmp(argv[1], "missing");
    if (argc == 2 && !strcmp(argv[1], "reset")) {
        api->kcg_init();
        api->shell_putchar('I', 7);
        api->shell_putchar('\n', 7);
        return 0;
    }
    api->kcg_init();
    if (glyphs(api, missing)) {
        api->shell_putchar('G', 7);
        api->shell_putchar('\n', 7);
        return 1;
    }
    api->kcg_set_scale(3);
    if (api->kcg_load_font("/fonts/shino16.kcg") != OS32_ERR_NOSYS) return 2;
    if (glyphs(api, missing)) return 3;
    api->shell_putchar('K', 7);
    api->shell_putchar('\n', 7);
    return 0;
}
