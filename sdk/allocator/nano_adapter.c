/* One break owner per executable. USER adds isolated nano arenas; resident
 * builds only the fixed primary backend and retains libc's nano entries. */
#include "nano_adapter.h"
#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifndef OS32_NANO_FIXTURE
#include "os32api.h"
extern KernelAPI *kapi;
extern char _end[];
#define CRT_CHUNK_ALIGN 4u
static struct os32_nano_arena primary_arena;
/* -1 permanently rejects a malformed initial handoff, including sbrk(0). */
static int primary_initialized;
#ifndef OS32_CRT_RESIDENT
static int crt_grow_exact(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque;
    if (!kapi->mem_map) return -1;
    return kapi->mem_map(bytes, (void *)base, OS32_MEM_MAP_EXACT) == (void *)base ? 0 : -1;
}
static void *crt_map(void *opaque, size_t bytes, unsigned flags)
{
    (void)opaque;
    return kapi->mem_map ? kapi->mem_map(bytes, NULL, flags) : NULL;
}
static int crt_unmap(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque;
    return kapi->mem_unmap ? kapi->mem_unmap((void *)base, bytes) : -1;
}
#endif
#endif

#ifndef OS32_CRT_RESIDENT
extern void *os32_private_malloc_r(struct _reent *, size_t);
extern void os32_private_free_r(struct _reent *, void *);
extern void *os32_private_calloc_r(struct _reent *, size_t, size_t);
extern void *os32_private_realloc_r(struct _reent *, void *, size_t);
extern size_t os32_private_malloc_usable_size_r(struct _reent *, void *);
extern struct mallinfo os32_private_mallinfo_r(struct _reent *);
extern void *os32_private_free_list;
extern char *os32_private_sbrk_start;
extern struct mallinfo os32_private_current_mallinfo;
static struct os32_nano_arena *selected, *arenas;
static void *(*map_arena)(void *, size_t, unsigned);
static int (*unmap_arena)(void *, uintptr_t, size_t);
static void *map_opaque;
static int busy;

#ifdef OS32_NANO_FIXTURE
int os32_nano_select(struct os32_nano_arena *arena)
{
    if (busy) return 0;
    selected = arena;
    return 1;
}

#endif

int os32_nano_configure(struct os32_nano_arena *primary,
                        void *(*map)(void *, size_t, unsigned),
                        int (*unmap)(void *, uintptr_t, size_t), void *opaque)
{
    if (busy || arenas) return 0;
    arenas = selected = primary;
    map_arena = map;
    unmap_arena = unmap;
    map_opaque = opaque;
    return 1;
}
#endif

#ifndef OS32_NANO_FIXTURE
static int initialize(struct _reent *r)
{
    if (!primary_initialized) {
        uintptr_t initial = (uintptr_t)&_end;
        primary_initialized = -1;
        if (initial > UINTPTR_MAX - (CRT_CHUNK_ALIGN - 1u)) goto fail;
        initial = (initial + CRT_CHUNK_ALIGN - 1u) & ~(CRT_CHUNK_ALIGN - 1u);
        if (kapi->sbrk_heap_limit < initial) goto fail;
        primary_arena.initial = primary_arena.brk = initial;
        primary_arena.mapped_end = kapi->sbrk_heap_limit;
#ifdef OS32_CRT_RESIDENT
        primary_arena.limit = kapi->sbrk_heap_limit;
        primary_arena.grow_exact = NULL;
#else
        primary_arena.limit = UINTPTR_MAX;
        primary_arena.grow_exact = crt_grow_exact;
        if (!os32_nano_configure(&primary_arena, crt_map, crt_unmap, NULL)) goto fail;
#endif
        primary_initialized = 1;
    }
    if (primary_initialized == 1) return 1;
fail:
    r->_errno = ENOMEM;
    return 0;
}

void *os32_nano_crt_sbrk(struct _reent *r, ptrdiff_t incr)
{
#ifndef OS32_CRT_RESIDENT
    if (busy) { r->_errno = ENOMEM; return (void *)-1; }
#endif
    if (!initialize(r)) return (void *)-1;
#ifndef OS32_CRT_RESIDENT
    /* The callback cannot reenter malloc or change the selected arena. */
    busy = 1;
    void *p = os32_nano_morecore(&primary_arena, r, incr);
    busy = 0;
    return p;
#else
    return os32_nano_morecore(&primary_arena, r, incr);
#endif
}
#endif

void *os32_nano_morecore(struct os32_nano_arena *a, struct _reent *r, ptrdiff_t incr)
{
    uintptr_t old, next, end;
    if (a == NULL) goto fail;
    old = a->brk;
    if (incr < 0) {
        /* Unsigned subtraction handles INT_MIN without signed negation. */
        uintptr_t decrease = 0u - (uintptr_t)incr;
        if (decrease > old - a->initial) goto fail;
        a->brk = old - decrease;
        return (void *)old;
    }
    if ((uintptr_t)incr > UINTPTR_MAX - old) goto fail;
    next = old + (uintptr_t)incr;
    if (next > a->limit) goto fail;
    if (next > a->mapped_end) {
        if (!a->grow_exact || next > UINTPTR_MAX - (OS32_NANO_PAGE - 1u)) goto fail;
        end = (next + OS32_NANO_PAGE - 1u) & ~(OS32_NANO_PAGE - 1u);
        if (end > a->limit) goto fail;
        if (a->grow_exact(a->opaque, a->mapped_end, end - a->mapped_end) != 0) goto fail;
        a->mapped_end = end;
    }
    a->brk = next;
    return (void *)old;
fail:
    r->_errno = ENOMEM;
    return (void *)-1;
}

#ifndef OS32_CRT_RESIDENT
static void load_state(void)
{
    os32_private_free_list = selected->free_list;
    os32_private_sbrk_start = selected->sbrk_start;
    os32_private_current_mallinfo = selected->info;
}
static void save_state(void)
{
    selected->free_list = os32_private_free_list;
    selected->sbrk_start = os32_private_sbrk_start;
    selected->info = os32_private_current_mallinfo;
}
static int enter(struct _reent *r)
{
#ifndef OS32_NANO_FIXTURE
    if (!busy && !initialize(r)) return 0;
#endif
    if (busy || selected == NULL) {
        r->_errno = ENOMEM;
        return 0;
    }
    busy = 1;
    load_state();
    return 1;
}
static void switch_arena(struct os32_nano_arena *a)
{
    save_state();
    selected = a;
    load_state();
}
static void leave(void)
{
    save_state();
    busy = 0;
}

void *os32_nano_sbrk(struct _reent *r, ptrdiff_t incr)
{
    return os32_nano_morecore(busy ? selected : NULL, r, incr);
}

/* Private nested calloc/realloc -> malloc/free stay in the same arena and
 * keep busy. Only the outer adapter switches, after nano has returned. */
void os32_nano_lock(struct _reent *r) { (void)r; }
void os32_nano_unlock(struct _reent *r) { (void)r; }

static struct os32_nano_arena *new_arena(size_t size)
{
    uintptr_t base;
    size_t bytes;
    struct os32_nano_arena *a;
    /* Management page + nano chunk header/padding + page rounding. */
    const size_t overhead = 2u * OS32_NANO_PAGE - 1u + 16u;
    if (!arenas || !map_arena || !unmap_arena || size > PTRDIFF_MAX - overhead) return NULL;
    bytes = (size + overhead) & ~(OS32_NANO_PAGE - 1u);
    base = (uintptr_t)map_arena(map_opaque, bytes, 0);
    if (!base) return NULL;
    a = (struct os32_nano_arena *)base;
    memset(a, 0, sizeof(*a));
    a->map_base = base;
    a->initial = a->brk = base + OS32_NANO_PAGE;
    a->mapped_end = base + bytes;
    a->limit = UINTPTR_MAX;
    a->grow_exact = arenas->grow_exact;
    a->opaque = arenas->opaque;
    a->next = arenas->next;
    arenas->next = a;
    return a;
}

static void release_empty(struct os32_nano_arena *a)
{
    struct os32_nano_arena **link, *next;
    uintptr_t base;
    size_t bytes;
    if (!arenas || a == arenas || a->live || !a->map_base) return;
    /* All fields needed after unmap are held outside the mapping. On failure
     * the list and saved nano state remain available for reuse/retry. */
    if (selected == a) switch_arena(arenas);
    for (link = &arenas->next; *link && *link != a; link = &(*link)->next) {}
    if (!*link) return;
    next = a->next;
    base = a->map_base;
    bytes = a->mapped_end - base;
    *link = next;
    if (unmap_arena(map_opaque, base, bytes) != 0) *link = a;
}

/* Layout/minimum of the pinned ILP32 nano-mallocr.c free chunk. Only nodes
 * reachable from the saved free list may be inspected (never live chunks). */
struct nano_free_chunk {
    long size;
    struct nano_free_chunk *next;
};
#define NANO_MINCHUNK 12u

size_t os32_nano_trim(void)
{
    struct os32_nano_arena **link;
    size_t pages = 0;
    if (busy || !arenas || !selected) return 0;
#ifndef OS32_NANO_FIXTURE
    if (primary_initialized != 1) return 0;
#endif
    busy = 1;
    load_state();
    save_state();
    for (link = &arenas; *link;) {
        struct os32_nano_arena *a = *link;
        size_t bytes;
        if (a != arenas && a->map_base && !a->live) {
            bytes = a->mapped_end - a->map_base;
            release_empty(a);
            if (*link != a) {
                pages += bytes / OS32_NANO_PAGE;
                continue; /* a is unmapped; do not read its next pointer. */
            }
        } else {
            struct nano_free_chunk *tail = a->free_list;
            uintptr_t keep, floor;
            if (tail) {
                while (tail->next) tail = tail->next;
                if ((uintptr_t)tail + tail->size == a->brk) {
                    keep = ((uintptr_t)tail + NANO_MINCHUNK + OS32_NANO_PAGE - 1u)
                           & ~(OS32_NANO_PAGE - 1u);
                    floor = (a->initial + OS32_NANO_PAGE - 1u) & ~(OS32_NANO_PAGE - 1u);
                    if (!a->map_base && keep < floor) keep = floor;
                    if (keep < a->mapped_end) {
                        bytes = a->mapped_end - keep;
                        /* Keep the node and all saved state until unmap succeeds. */
                        if (unmap_arena(map_opaque, keep, bytes) == 0) {
                            tail->size = keep - (uintptr_t)tail;
                            a->brk = a->mapped_end = keep;
                            pages += bytes / OS32_NANO_PAGE;
                        }
                    }
                }
            }
        }
        link = &a->next;
    }
    load_state();
    leave();
    return pages;
}

/* The list lives in USER mappings. Only an exact payload match permits
 * reading its prefix; arbitrary caller pointers are never dereferenced. */
#define LARGE_MAGIC 0x4c415247u
#define LARGE_KIND 1u
struct large_block {
    struct large_block *next;
    uintptr_t base;
    size_t map_bytes, requested;
    unsigned magic, kind, alignment, reserved;
} __attribute__((aligned(8)));
static struct large_block *large_blocks;

static struct large_block **large_link(const void *p)
{
    struct large_block **link;
    for (link = &large_blocks; *link; link = &(*link)->next) {
        if ((const void *)(*link + 1) == p) return link;
    }
    return NULL;
}
static int large_valid(struct _reent *r, struct large_block *b)
{
#ifdef OS32_NANO_FIXTURE
    extern void os32_nano_prefix_check(const void *);
    os32_nano_prefix_check(b);
#endif
    if (b->magic == LARGE_MAGIC && b->kind == LARGE_KIND &&
        b->alignment == 8u && b->base == (uintptr_t)b &&
        b->requested >= OS32_NANO_LARGE &&
        b->map_bytes >= sizeof(*b) && b->requested <= b->map_bytes - sizeof(*b) &&
        !(b->map_bytes & (OS32_NANO_PAGE - 1u))) return 1;
    r->_errno = EINVAL;
    return 0;
}
static void *large_allocate(struct _reent *r, size_t bytes)
{
    size_t total;
    struct large_block *b;
    if (bytes > SIZE_MAX - sizeof(*b)) goto fail;
    total = bytes + sizeof(*b);
    if (total > SIZE_MAX - (OS32_NANO_PAGE - 1u)) goto fail;
    total = (total + OS32_NANO_PAGE - 1u) & ~(OS32_NANO_PAGE - 1u);
    if (!map_arena || !unmap_arena) goto fail;
    b = map_arena(map_opaque, total, OS32_NANO_TOPDOWN);
    if (!b) goto fail;
    b->next = large_blocks;
    b->base = (uintptr_t)b;
    b->map_bytes = total;
    b->requested = bytes;
    b->magic = LARGE_MAGIC;
    b->kind = LARGE_KIND;
    b->alignment = 8u;
    b->reserved = 0;
    large_blocks = b;
    /* calloc relies on fresh app pages being zeroed by paging_app.c.
     * Only the prefix is written here; recycled VA must also get fresh zeros. */
    return b + 1;
fail:
    r->_errno = ENOMEM;
    return NULL;
}
static int large_release(struct _reent *r, struct large_block **link)
{
    struct large_block *b = *link, *next;
    uintptr_t base;
    size_t bytes;
    if (!large_valid(r, b)) return 0;
    next = b->next;
    base = b->base;
    bytes = b->map_bytes;
    *link = next;
    if (unmap_arena(map_opaque, base, bytes) != 0) {
        *link = b;
        r->_errno = ENOMEM;
        return 0;
    }
    return 1;
}

static void *try_allocate(struct _reent *r, size_t n, size_t size, int clear)
{
    void *p;
    if (clear) p = os32_private_calloc_r(r, n, size);
    else p = os32_private_malloc_r(r, size);
    if (p) selected->live++;
    return p;
}
static __attribute__((noinline)) void *allocate(struct _reent *r, size_t n, size_t size, int clear,
                      struct os32_nano_arena *exclude)
{
    struct os32_nano_arena *a;
    void *p = NULL;
    size_t bytes;
    if (n && size > SIZE_MAX / n) { r->_errno = ENOMEM; return NULL; }
    bytes = n * size;
    if (bytes >= OS32_NANO_LARGE) return large_allocate(r, bytes);
    /* Explicit standalone fixtures may select just one backing arena. */
    if (!arenas) return try_allocate(r, n, size, clear);
    for (a = arenas; a; a = a->next) {
        if (a == exclude) continue;
        switch_arena(a);
        p = try_allocate(r, n, size, clear);
        if (p) return p;
    }
    a = new_arena(bytes);
    if (a) {
        switch_arena(a);
        p = try_allocate(r, n, size, clear);
        if (!p) release_empty(a);
    }
    if (!p) r->_errno = ENOMEM;
    return p;
}

void *_malloc_r(struct _reent *r, size_t size)
{
    void *p;
    if (!enter(r)) return NULL;
    p = allocate(r, 1, size, 0, NULL);
    leave();
    return p;
}
static struct os32_nano_arena *owner(struct _reent *r, const void *p)
{
    struct os32_nano_arena *a;
    for (a = arenas ? arenas : selected; a; a = a->next) {
        uintptr_t start = a->sbrk_start ? (uintptr_t)a->sbrk_start : a->initial;
        if ((uintptr_t)p >= start && (uintptr_t)p < a->brk) return a;
    }
    r->_errno = EINVAL;
    return NULL;
}

void _free_r(struct _reent *r, void *p)
{
    struct os32_nano_arena *a;
    if (!enter(r)) return;
    struct large_block **link = p ? large_link(p) : NULL;
    if (link) large_release(r, link);
    else if (p && (a = owner(r, p)) != NULL) {
        switch_arena(a);
        os32_private_free_r(r, p);
        a->live--;
        release_empty(a);
    }
    leave();
}
void *_calloc_r(struct _reent *r, size_t n, size_t size)
{
    void *p;
    if (!enter(r)) return NULL;
    p = allocate(r, n, size, 1, NULL);
    leave();
    return p;
}
void *_realloc_r(struct _reent *r, void *old, size_t size)
{
    struct os32_nano_arena *a;
    void *p = NULL;
    size_t old_size;
    if (!enter(r)) return NULL;
    if (!old) {
        p = allocate(r, 1, size, 0, NULL);
    } else if (large_link(old)) {
        struct large_block **link = large_link(old);
        struct large_block *b = *link;
        if (large_valid(r, b)) {
            if (!size) large_release(r, link);
            else {
                old_size = b->requested;
                p = allocate(r, 1, size, 0, NULL);
                if (p) {
                    memcpy(p, old, old_size < size ? old_size : size);
                    /* Allocation may prepend another large block. */
                    large_release(r, large_link(old));
                }
            }
        }
    } else if ((a = owner(r, old)) != NULL) {
        switch_arena(a);
        old_size = os32_private_malloc_usable_size_r(r, old);
        if (size >= OS32_NANO_LARGE) {
            p = allocate(r, 1, size, 0, NULL);
            if (p) {
                memcpy(p, old, old_size < size ? old_size : size);
                os32_private_free_r(r, old);
                a->live--;
                release_empty(a);
            }
            leave();
            return p;
        }
        p = os32_private_realloc_r(r, old, size);
        if (!size) {
            a->live--;
            release_empty(a);
        } else if (!p && arenas) {
            p = allocate(r, 1, size, 0, a);
            if (p) {
                memcpy(p, old, old_size < size ? old_size : size);
                switch_arena(a);
                os32_private_free_r(r, old);
                a->live--;
                release_empty(a);
            }
        }
    }
    leave();
    return p;
}
#ifdef OS32_NANO_FIXTURE
struct mallinfo os32_nano_info(struct _reent *r)
{
    struct mallinfo info = {0};
    if (!enter(r)) return info;
    info = os32_private_mallinfo_r(r);
    leave();
    return info;
}
#endif
void *malloc(size_t size) { return _malloc_r(_impure_ptr, size); }
void free(void *p) { _free_r(_impure_ptr, p); }
void *calloc(size_t n, size_t size) { return _calloc_r(_impure_ptr, n, size); }
void *realloc(void *p, size_t size) { return _realloc_r(_impure_ptr, p, size); }
#endif
