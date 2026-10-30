#include <stdio.h>
#include "lib/unicode_jis_table.h"

#include <string.h>
#include "endian_le.h"
#include "kernel/console/unicode_file.h"

/* Product file: headerless, strictly ordered LE pairs. */
static int write_pairs(const char *path)
{
    STATIC_ASSERT(UNICODE_JIS_TABLE_SIZE == UNICODE_FILE_PAIRS, unicode_pair_count);
    FILE *f = fopen(path, "wb");
    if (!f) return 1;
    for (int i = 0; i < UNICODE_JIS_TABLE_SIZE; i++) {
        u8 pair[UNICODE_FILE_PAIR_BYTES];
        le16_wr(pair, unicode_jis_table[i].unicode);
        le16_wr(pair + 2, unicode_jis_table[i].jis);
        if (fwrite(pair, 1, sizeof(pair), f) != sizeof(pair)) {
            fclose(f);
            return 1;
        }
    }
    return fclose(f) != 0;
}

int main(int argc, char **argv) {
    if (argc == 3 && strcmp(argv[1], "--pairs") == 0)
        return write_pairs(argv[2]);
    if (argc != 1) {
        fprintf(stderr, "usage: %s [--pairs output]\n", argv[0]);
        return 1;
    }
    return write_pairs("unicode.bin");
}
