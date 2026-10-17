/* T4a-3 host prototype only; no product allocator or reserve policy. */
#ifndef SQLITE_MEMSYS5_PROTO_H
#define SQLITE_MEMSYS5_PROTO_H

#include "sqlite3.h"

enum { P5_BUDGET = 512 * 1024, P5_TAGS = 4096, P5_PREFIX_BYTES = 8 };
enum { P5_SYSTEM, P5_FEP, P5_APP, P5_OWNERS };
enum { P5_PREFIX, P5_TABLE };

typedef struct {
    int heap_bytes, tag_bytes, state_bytes, live, peak_live;
    int used[P5_OWNERS], peak_bytes;
    int malloc_calls, realloc_calls, free_calls, table_denials;
    int init_calls, shutdown_calls;
} P5Stats;

int p5_begin(int mode);
int p5_end(void);
void p5_context(int owner);
P5Stats p5_stats(void);
int p5_in_heap(void *p);

#endif
