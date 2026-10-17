/* Allocation permission/accounting experiment for TASK_T4_T6_MODULES §3-3.
 * All per-allocation tags are inside storage. Counters are observer state.
 * A single cooperative caller sets context before an outer engine call.
 */
#include <assert.h>
#include <string.h>
#include "sqlite_memsys5_proto.h"

typedef struct { void *ptr; int owner; } Tag;
typedef struct { int owner; int reserved; } Prefix;
typedef struct {
    sqlite3_mem_methods underlying;
    P5Stats stats;
    int mode, context;
} State;
static State state;
enum { STATE_BYTES = (sizeof(State) + 7) & ~7 };
static union {
    sqlite3_uint64 align;
    unsigned char bytes[P5_BUDGET - STATE_BYTES];
} storage;

_Static_assert(sizeof(void *) == 4, "measure OS32 ILP32 tags");
_Static_assert(sizeof(Prefix) == P5_PREFIX_BYTES, "8-byte alignment");
_Static_assert(sizeof(storage) + sizeof(State) <= P5_BUDGET, "all wrapper state in budget");
_Static_assert(P5_TAGS * sizeof(Tag) + STATE_BYTES < P5_BUDGET, "tags leave one heap");

static Tag *tags(void) { return (Tag *)storage.bytes; }
static void *base_of(void *p) { return state.mode == P5_PREFIX ? (Prefix *)p - 1 : p; }

static Tag *find_tag(void *p)
{
    for (int i = 0; i < P5_TAGS; ++i)
        if (tags()[i].ptr == p) return &tags()[i];
    return 0;
}

static int allocation_owner(void *p)
{
    if (state.mode == P5_PREFIX) return ((Prefix *)p - 1)->owner;
    Tag *tag = find_tag(p);
    assert(tag);
    return tag->owner;
}

static int charged_size(void *base)
{
    return state.underlying.xSize(base);
}

static void account(int owner, int delta)
{
    state.stats.used[owner] += delta;
    /* 負の勘定は試験の CHECK が落とす (ここで abort すると変異が RED に数えられない)。 */
    int total = 0;
    for (int i = 0; i < P5_OWNERS; ++i) total += state.stats.used[i];
    if (total > state.stats.peak_bytes) state.stats.peak_bytes = total;
}

static int rounded_raw(int n)
{
    int prefix = state.mode == P5_PREFIX ? sizeof(Prefix) : 0;
    /* MEMSYS5's largest representable buddy is 1GiB, even for this tiny heap. */
    if (n <= 0 || n > 0x40000000 - prefix) return 0;
    return state.underlying.xRoundup(n + prefix);
}

static int wrapped_roundup(int n)
{
    int raw = rounded_raw(n);
    return raw ? raw - (state.mode == P5_PREFIX ? sizeof(Prefix) : 0) : 0;
}

static void *wrapped_malloc(int n)
{
    int raw = rounded_raw(n);
    if (!raw) return 0;
    Tag *tag = 0;
    if (state.mode == P5_TABLE) {
        tag = find_tag(0);
        if (!tag) {
            state.stats.table_denials++;
            return 0;
        }
    }
    state.stats.malloc_calls++;
    void *base = state.underlying.xMalloc(raw);
    if (!base) return 0;
    void *p = base;
    if (state.mode == P5_PREFIX) {
        Prefix *prefix = base;
        prefix->owner = state.context;
        prefix->reserved = 0;
        p = prefix + 1;
    } else {
        tag->ptr = p;
        tag->owner = state.context;
    }
    account(state.context, charged_size(base));
    if (++state.stats.live > state.stats.peak_live) state.stats.peak_live = state.stats.live;
    return p;
}

static void wrapped_free(void *p)
{
    if (!p) return;
    int owner = allocation_owner(p);
    void *base = base_of(p);
    account(owner, -charged_size(base));
    if (state.mode == P5_TABLE) find_tag(p)->ptr = 0;
    state.stats.live--;
    state.stats.free_calls++;
    state.underlying.xFree(base);
}

static int wrapped_size(void *p)
{
    return state.underlying.xSize(base_of(p)) - (state.mode == P5_PREFIX ? sizeof(Prefix) : 0);
}

static void *wrapped_realloc(void *p, int n)
{
    assert(p);
    int raw = rounded_raw(n);
    if (!raw) return 0;
    int owner = allocation_owner(p);
    void *base = base_of(p);
    int old_size = charged_size(base);
    Tag *tag = state.mode == P5_TABLE ? find_tag(p) : 0;
    state.stats.realloc_calls++;
    void *next = state.underlying.xRealloc(base, raw);
    if (!next) return 0; /* Old bytes, tag and charges stay untouched. */
    account(owner, charged_size(next) - old_size);
    if (state.mode == P5_PREFIX) return (Prefix *)next + 1;
    tag->ptr = next; /* Reuse the allocation's slot even if the table is full. */
    return next;
}

static int wrapped_init(void *arg)
{
    (void)arg;
    state.stats.init_calls++;
    return state.underlying.xInit(state.underlying.pAppData);
}

static void wrapped_shutdown(void *arg)
{
    (void)arg;
    state.stats.shutdown_calls++;
    state.underlying.xShutdown(state.underlying.pAppData);
}

int p5_begin(int tagging)
{
    int rc = sqlite3_shutdown();
    if (rc != SQLITE_OK) return rc;
    memset(&state, 0, sizeof(state));
    memset(&storage, 0, sizeof(storage));
    state.mode = tagging;
    state.context = P5_SYSTEM;
    state.stats.tag_bytes = state.mode == P5_TABLE ? P5_TAGS * sizeof(Tag) : 0;
    state.stats.state_bytes = STATE_BYTES;
    state.stats.heap_bytes = P5_BUDGET - state.stats.tag_bytes - STATE_BYTES;
    rc = sqlite3_config(SQLITE_CONFIG_HEAP, storage.bytes + state.stats.tag_bytes,
                        state.stats.heap_bytes, 32);
    if (rc != SQLITE_OK) return rc;
    rc = sqlite3_config(SQLITE_CONFIG_GETMALLOC, &state.underlying);
    if (rc != SQLITE_OK) return rc;
    sqlite3_mem_methods wrapper = {
        wrapped_malloc, wrapped_free, wrapped_realloc, wrapped_size,
        wrapped_roundup, wrapped_init, wrapped_shutdown, 0
    };
    rc = sqlite3_config(SQLITE_CONFIG_MALLOC, &wrapper);
    return rc == SQLITE_OK ? sqlite3_initialize() : rc;
}

int p5_end(void) { return sqlite3_shutdown(); }
void p5_context(int owner) { assert(owner >= 0 && owner < P5_OWNERS); state.context = owner; }
P5Stats p5_stats(void) { return state.stats; }
int p5_in_heap(void *p)
{
    unsigned char *b = base_of(p);
    return b >= storage.bytes + state.stats.tag_bytes && b < storage.bytes + sizeof(storage.bytes);
}
