#include <stdio.h>
#include "lib/unicode_jis_table.h"

#include <stdlib.h>
#include <string.h>
#include "endian_le.h"
#include "kernel/unicode_file.h"

/* T3d1a opt-in output, kept separate from the deployed flat unicode.bin.
 * d1b will switch the product rule to this format. */
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
    FILE *f = fopen("unicode.bin", "wb");
    if (!f) return 1;
    
    unsigned short *table = calloc(65536, sizeof(unsigned short));
    int i;
    for (i = 0; i < UNICODE_JIS_TABLE_SIZE; i++) {
        table[unicode_jis_table[i].unicode] = unicode_jis_table[i].jis;
    }
    
    size_t w = fwrite(table, sizeof(unsigned short), 65536, f);
    fclose(f);
    free(table);
    
    printf("Successfully wrote unicode.bin (%zu elements, %zu bytes)\n", w, w * sizeof(unsigned short));
    return 0;
}
