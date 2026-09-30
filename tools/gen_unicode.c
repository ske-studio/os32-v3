#include <stdio.h>
#include "lib/unicode_jis_table.h"

#include <stdlib.h>
#include <string.h>

int main() {
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
