/* f1b: isolated connection to pinned newlib nano objects. No public build
 * consumes this file yet. See TASK_T2D_T2H.md section 3-5. */
#include "nano_adapter.h"
#include <errno.h>
#include <stdlib.h>

extern void *os32_private_malloc_r(struct _reent *, size_t);
extern void os32_private_free_r(struct _reent *, void *);
extern void *os32_private_calloc_r(struct _reent *, size_t, size_t);
extern void *os32_private_realloc_r(struct _reent *, void *, size_t);
extern struct mallinfo os32_private_mallinfo_r(struct _reent *);
extern void *os32_private_free_list;
extern char *os32_private_sbrk_start;
extern struct mallinfo os32_private_current_mallinfo;
static struct os32_nano_arena *selected;
static int busy;

int os32_nano_select(struct os32_nano_arena *arena)
{
    if (busy) return 0;
    selected = arena;
    return 1;
}

static int enter(struct _reent *r)
{
    if (busy || selected == NULL) {
        r->_errno = ENOMEM;
        return 0;
    }
    busy = 1;
    os32_private_free_list = selected->free_list;
    os32_private_sbrk_start = selected->sbrk_start;
    os32_private_current_mallinfo = selected->info;
    return 1;
}

static void leave(void)
{
    selected->free_list = os32_private_free_list;
    selected->sbrk_start = os32_private_sbrk_start;
    selected->info = os32_private_current_mallinfo;
    busy = 0;
}

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

void *os32_nano_sbrk(struct _reent *r, ptrdiff_t incr)
{
    return os32_nano_morecore(busy ? selected : NULL, r, incr);
}

/* These hooks are private to the transformed objects. The public entry owns
 * busy across nested nano calloc/realloc -> malloc/free, not each nano lock.
 * Callback reentry through any public entry is rejected before state loads. */
void os32_nano_lock(struct _reent *r) { (void)r; }
void os32_nano_unlock(struct _reent *r) { (void)r; }

void *_malloc_r(struct _reent *r, size_t size)
{
    void *p;
    if (!enter(r)) return NULL;
    p = os32_private_malloc_r(r, size);
    leave();
    return p;
}
static int owns_pointer(struct _reent *r, const void *p)
{
    uintptr_t start = selected->sbrk_start ? (uintptr_t)selected->sbrk_start : selected->initial;
    if (p != NULL && ((uintptr_t)p < start || (uintptr_t)p >= selected->brk)) {
        r->_errno = EINVAL;
        return 0;
    }
    return 1;
}

void _free_r(struct _reent *r, void *p)
{
    if (!enter(r)) return;
    if (owns_pointer(r, p)) os32_private_free_r(r, p);
    leave();
}
void *_calloc_r(struct _reent *r, size_t n, size_t size)
{
    void *p;
    if (!enter(r)) return NULL;
    p = os32_private_calloc_r(r, n, size);
    leave();
    return p;
}
void *_realloc_r(struct _reent *r, void *old, size_t size)
{
    void *p;
    if (!enter(r)) return NULL;
    p = owns_pointer(r, old) ? os32_private_realloc_r(r, old, size) : NULL;
    leave();
    return p;
}
struct mallinfo os32_nano_info(struct _reent *r)
{
    struct mallinfo info = {0};
    if (!enter(r)) return info;
    info = os32_private_mallinfo_r(r);
    leave();
    return info;
}
void *malloc(size_t size) { return _malloc_r(_impure_ptr, size); }
void free(void *p) { _free_r(_impure_ptr, p); }
void *calloc(size_t n, size_t size) { return _calloc_r(_impure_ptr, n, size); }
void *realloc(void *p, size_t size) { return _realloc_r(_impure_ptr, p, size); }
