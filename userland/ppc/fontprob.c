#include <stdio.h>
#include "os32api.h"
#include "../../include/config.h"

enum {
    APP_LEASES = 3,
    JIS_A = 0x3021,
    KCG_HEADER_BYTES = 16,
    KCG_JIS_COLUMNS = 94,
    KCG_GLYPH_BYTES = 32,
    A_OFFSET = KCG_HEADER_BYTES + (0x30 - 0x21) * KCG_JIS_COLUMNS * KCG_GLYPH_BYTES,
};

static int probe(KernelAPI *api, int leave_open)
{
    /* 東雲16 の「亜」(U+4E9C、JIS 3021) の 16 本の走査線。 */
    static const u8 expected[KCG_GLYPH_BYTES] = {
        0x00, 0x00, 0x3f, 0xff, 0x02, 0x20, 0x02, 0x20,
        0x02, 0x20, 0x1f, 0xfc, 0x12, 0x24, 0x12, 0x24,
        0x12, 0x24, 0x12, 0x24, 0x1f, 0xfc, 0x02, 0x20,
        0x02, 0x20, 0x02, 0x20, 0x7f, 0xff, 0x00, 0x00,
    };
    u32 ids[APP_LEASES], size;
    u8 glyph[KCG_GLYPH_BYTES];

    for (int i = 0; i < APP_LEASES; ++i) {
        int rc = api->font_source_open(SYS_FB_FONT, &ids[i], &size);
        if (rc == OS32_ERR_NOSYS)
            return 10;
        if (rc != 0)
            return 11;
    }
    if (leave_open)
        return 0;

    if (api->unicode_to_jis(0x4e9c) != JIS_A)
        return 12;
    if (api->font_source_read_at(ids[0], A_OFFSET, glyph, sizeof(glyph)) != sizeof(glyph))
        return 13;
    for (u32 i = 0; i < sizeof(glyph); ++i) {
        if (glyph[i] != expected[i])
            return 14;
    }
    if (api->font_source_read_at(ids[0], size, glyph, 0) != 0 ||
        api->font_source_read_at(ids[0], size, glyph, 1) != OS32_ERR_INVAL)
        return 15;

    for (int i = 0; i < APP_LEASES; ++i) {
        if (api->font_source_close(ids[i]) != 0)
            return 16;
    }
    if (api->font_source_read_at(ids[0], A_OFFSET, glyph, 1) != OS32_ERR_STALE)
        return 17;
    return 0;
}

int main(int argc, char **argv, KernelAPI *api)
{
    (void)argv;
    int status = probe(api, argc == 2);
    printf("fontprob=%d\n", status);
    return status;
}
