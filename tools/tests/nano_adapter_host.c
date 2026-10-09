/* Real nano objects, freestanding ILP32 Linux entry; no host libc allocator. */
#include "nano_adapter.h"
#include "os32api.h"
#include "os32_gui_shared.h"
static KernelAPI mock_api;
KernelAPI *kapi = &mock_api;
#include <stdlib.h>
#include <string.h>
#include <errno.h>
extern void *_reallocf_r(struct _reent *, void *, size_t);
static struct _reent reent;
struct _reent *_impure_ptr = &reent;
static unsigned char storage[4 * OS32_NANO_PAGE] __attribute__((aligned(OS32_NANO_PAGE)));
static struct os32_nano_arena a, b;
static int calls, fail_call, reenter, reentry_ok;
static unsigned yields, done_calls, serve_probe;
static uintptr_t map_base;
static size_t map_bytes;
static int checks;
void *memset(void *p, int c, size_t n) { unsigned char *q=p; while(n--) *q++=c; return p; }
void *memcpy(void *p, const void *s, size_t n) { unsigned char *q=p; const unsigned char *r=s; while(n--) *q++=*r++; return p; }
static void write_text(const char *s, size_t n)
{
    int result=4;
    __asm__ volatile("int $0x80" : "+a"(result) : "b"(1), "c"(s), "d"(n) : "memory");
}
static void stop(int code) __attribute__((noreturn));
static void stop(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    __builtin_unreachable();
}
static void number(unsigned n)
{
    char buf[16]; unsigned pos=sizeof(buf);
    do { buf[--pos]='0'+n%10; n/=10; } while(n);
    write_text(buf+pos,sizeof(buf)-pos);
}
#define CHECK(x) do { ++checks; if (!(x)) { write_text("FAIL line ",10); number(__LINE__); write_text(" check: " #x "\n", sizeof(" check: " #x "\n")-1); stop(1); } } while(0)
static int grow(void *opaque, uintptr_t base, size_t bytes)
{
    CHECK(opaque == &a);
    calls++; map_base=base; map_bytes=bytes;
    if (reenter) {
        struct _reent nested = {0};
        struct mallinfo info;
        CHECK(!os32_nano_select(&b));
        CHECK(_malloc_r(&nested, 8) == NULL && nested._errno == ENOMEM);
        CHECK(_calloc_r(&nested, 1, 8) == NULL);
        CHECK(_realloc_r(&nested, NULL, 8) == NULL);
        _free_r(&nested, NULL);
        CHECK(malloc(8) == NULL && calloc(1,8) == NULL && realloc(NULL,8) == NULL);
        free(NULL);
        info=os32_nano_info(&nested);
        CHECK(info.arena == 0);
        reentry_ok++;
    }
    return calls == fail_call ? -1 : 0;
}
static void init(struct os32_nano_arena *arena, uintptr_t base, size_t mapped, size_t limit)
{
    memset(arena,0,sizeof(*arena));
    arena->initial=arena->brk=base;
    arena->mapped_end=base+mapped;
    arena->limit=base+limit;
}
static void reset(void)
{
    memset(storage,0xA5,sizeof(storage));
    init(&a,(uintptr_t)storage,OS32_NANO_PAGE,2*OS32_NANO_PAGE);
    init(&b,(uintptr_t)storage+2*OS32_NANO_PAGE,OS32_NANO_PAGE,2*OS32_NANO_PAGE);
    CHECK(os32_nano_select(&a));
    calls=fail_call=reenter=reentry_ok=0;
    reent._errno=0;
}
static void tests(void)
{
    void *p, *q, *r; uintptr_t old; struct mallinfo ai,bi;
    reset();
    old=a.brk;
    CHECK(os32_nano_sbrk(&reent,16)==(void*)-1 && a.brk==old);
    CHECK(os32_nano_morecore(&a,&reent,16)==(void*)old && a.brk==old+16);
    CHECK(os32_nano_morecore(&a,&reent,-16)==(void*)(old+16) && a.brk==old);
    CHECK(os32_nano_morecore(&a,&reent,-1)==(void*)-1 && a.brk==old);
    CHECK(os32_nano_morecore(&a,&reent,(ptrdiff_t)0x80000000u)==(void*)-1 && a.brk==old);
    CHECK(a.mapped_end==old+OS32_NANO_PAGE);
    init(&a,0xfffffff0u,15,15);
    CHECK(os32_nano_morecore(&a,&reent,32)==(void*)-1 && a.brk==0xfffffff0u);
    CHECK(os32_nano_morecore(&a,&reent,15)==(void*)0xfffffff0u && a.brk==UINTPTR_MAX);
    CHECK(os32_nano_morecore(&a,&reent,0)==(void*)UINTPTR_MAX);
    init(&a,0xfffff000u,0,OS32_NANO_PAGE-1);
    a.grow_exact=grow; a.opaque=&a;
    CHECK(os32_nano_morecore(&a,&reent,1)==(void*)-1 && calls==0);
    reset();
    a.mapped_end=a.initial; a.limit=a.initial+OS32_NANO_PAGE-1; a.grow_exact=grow; a.opaque=&a;
    CHECK(os32_nano_morecore(&a,&reent,1)==(void*)-1 && calls==0 && a.brk==a.initial);
    reset();
    p=malloc(16); CHECK(p && !((uintptr_t)p&7));
    memset(p,0x37,16); q=calloc(4,8); CHECK(q && ((unsigned char*)q)[0]==0 && ((unsigned char*)q)[31]==0);
    r=realloc(p,128); CHECK(r && ((unsigned char*)r)[0]==0x37 && ((unsigned char*)r)[15]==0x37);
    free(q); free(r); free(NULL);
    CHECK(a.free_list != NULL);
    CHECK(_calloc_r(&reent,0x80000000u,2)==NULL && reent._errno==ENOMEM);
    CHECK(_malloc_r(&reent,(size_t)-1)==NULL);
    reset(); /* Hole reuse and live neighbor preservation. */
    p=_malloc_r(&reent,64); q=_malloc_r(&reent,64); memset(q,0x42,64);
    _free_r(&reent,p); old=a.brk; r=_malloc_r(&reent,32);
    CHECK(r==p && a.brk==old && ((unsigned char*)q)[63]==0x42);
    reset(); /* sbrk_aligned second request, deliberately unaligned break. */
    a.initial=a.brk=(uintptr_t)storage+1;
    p=malloc(16); CHECK(p && a.brk==(uintptr_t)storage+28 && !((uintptr_t)p&7));
    reset();
    a.initial=a.brk=(uintptr_t)storage+1; a.limit=(uintptr_t)storage+25;
    p=malloc(16); CHECK(p==NULL && a.brk==(uintptr_t)storage+25);
    CHECK(a.sbrk_start==(char*)storage+1 && a.free_list==NULL);
    a.limit=(uintptr_t)storage+OS32_NANO_PAGE;
    p=malloc(16); CHECK(p && (uintptr_t)p>(uintptr_t)storage+25);
    reset(); /* nano's full request fails, tail difference succeeds. */
    p=malloc(16); free(p); a.limit=(uintptr_t)storage+40;
    q=malloc(32); CHECK(q==p && a.brk==(uintptr_t)storage+40 && !a.free_list);
    reset(); p=malloc(16); free(p); old=a.brk; a.limit=old;
    CHECK(malloc(32)==NULL && a.brk==old && a.free_list);
    CHECK(malloc(16)==p); /* Failure must retain the old free chunk. */
    reset(); /* USER page growth / exact failure: no break publication. */
    a.grow_exact=grow; a.opaque=&a;
    p=malloc(OS32_NANO_PAGE); CHECK(p && calls==1);
    CHECK(map_base==(uintptr_t)storage+OS32_NANO_PAGE && map_bytes==OS32_NANO_PAGE);
    CHECK(a.mapped_end==(uintptr_t)storage+2*OS32_NANO_PAGE);
    reset(); a.grow_exact=grow; a.opaque=&a; fail_call=1; old=a.brk;
    CHECK(malloc(OS32_NANO_PAGE)==NULL && a.brk==old && a.mapped_end==old+OS32_NANO_PAGE);
    CHECK(calls==1); CHECK(malloc(16)!=NULL);
    reset(); /* Resident never calls map even at the same USER-sized request. */
    CHECK(malloc(OS32_NANO_PAGE)==NULL && calls==0 && a.brk==a.initial);
    reset(); /* Public callback reentry cannot change arena or nano state. */
    a.grow_exact=grow; a.opaque=&a; reenter=1;
    p=_calloc_r(&reent,1,OS32_NANO_PAGE); CHECK(p && reentry_ok==1);
    CHECK(((unsigned char*)p)[OS32_NANO_PAGE-1]==0 && b.brk==b.initial);
    free(p); CHECK(a.free_list);
    reset(); /* Separate, adjacent arenas, no shared free list/statistics. */
    a.limit=a.mapped_end=(uintptr_t)storage+2*OS32_NANO_PAGE;
    a.info.hblks=17; b.info.hblks=29;
    p=malloc(24); memset(p,0x55,24); q=malloc(40); free(q);
    ai=os32_nano_info(&reent); CHECK(ai.arena==(size_t)(a.brk-a.initial) && ai.fordblks>0);
    CHECK(ai.hblks==17 && a.info.arena==ai.arena);
    old=a.brk; CHECK(os32_nano_select(&b));
    q=_calloc_r(&reent,2,32); CHECK(q && (uintptr_t)q>=b.initial);
    r=_realloc_r(&reent,q,96); CHECK(r && ((unsigned char*)r)[63]==0);
    _free_r(&reent,r); bi=os32_nano_info(&reent);
    CHECK(bi.hblks==29 && b.info.arena==bi.arena);
    CHECK(bi.arena==(size_t)(b.brk-b.initial) && b.free_list!=a.free_list);
    CHECK(a.brk==old && ((unsigned char*)p)[23]==0x55);
    CHECK(os32_nano_select(&a)); bi=os32_nano_info(&reent);
    CHECK(bi.arena==ai.arena && bi.fordblks==ai.fordblks);
    free(p); CHECK(malloc(24)==p);
    reset(); /* Fill a through its limit: its final chunk touches b's first. */
    a.limit=a.mapped_end=b.initial;
    p=malloc(2*OS32_NANO_PAGE-8);
    CHECK(p && a.brk==b.initial);
    CHECK(os32_nano_select(&b));
    q=malloc(24); memset(q,0x5a,24);
    CHECK(q && (uintptr_t)q-8==b.initial);
    CHECK(os32_nano_select(&a));
    _free_r(&reent,q);
    CHECK(a.free_list==NULL && reent._errno==EINVAL);
    CHECK(_realloc_r(&reent,q,64)==NULL && ((unsigned char*)q)[23]==0x5a && a.free_list==NULL);
    free(p);
    CHECK(os32_nano_select(&b)); free(q);
    ai=os32_nano_info(&reent);
    CHECK(ai.fordblks==(size_t)(b.brk-b.initial));
    CHECK(os32_nano_select(&a)); ai=os32_nano_info(&reent);
    CHECK(ai.fordblks==2*OS32_NANO_PAGE && a.brk==b.initial);
    CHECK(malloc(2*OS32_NANO_PAGE-8)==p);
    reset(); /* Actual libc reallocf caller must resolve to public adapter. */
    p=malloc(32); memset(p,0x6c,32); a.limit=a.brk;
    CHECK(_reallocf_r(&reent,p,128)==NULL && a.free_list!=NULL);
    CHECK(malloc(32)==p && ((unsigned char*)p)[31]==0x6c);
    reset(); p=malloc(32); memset(p,0x6b,32); a.limit=a.brk;
    CHECK(realloc(p,128)==NULL && ((unsigned char*)p)[31]==0x6b);
    CHECK(realloc(p,0)==NULL && a.free_list);
    CHECK(os32_nano_select(NULL)); CHECK(malloc(16)==NULL);
}
/* Automatic routing uses management pages and the real private nano objects. */
#define MAP_SLOTS 4
#define MAP_CAPACITY (64u * OS32_NANO_PAGE)
static unsigned char secondary[MAP_SLOTS][MAP_CAPACITY] __attribute__((aligned(OS32_NANO_PAGE)));
static size_t mapped[MAP_SLOTS];
static unsigned maps, unmaps, last_flags, attempts;
static int reject_map, reject_unmap;
static int trim_fixture;
static uintptr_t primary_end, trim_fail_base, trim_unmap_base;
static size_t trim_unmap_bytes, grow_ceiling;
static unsigned trim_unmaps, exact_calls, exact_failures;
static int trim_callback_depth;
static uintptr_t exact_base;
static size_t exact_bytes;
static uintptr_t page_up(uintptr_t p)
{
    return (p+OS32_NANO_PAGE-1u) & ~(OS32_NANO_PAGE-1u);
}
static void callback_checks(void)
{
    struct _reent nested = {0};
    unsigned before_yield=yields;
    unsigned before_refused=os32_gui_retry_stats().retry_refused_count;
    CHECK(!os32_nano_select(&b));
    CHECK(!os32_nano_configure(&b, NULL, NULL, NULL));
    CHECK(_malloc_r(&nested, 8) == NULL && nested._errno == ENOMEM);
    CHECK(_realloc_r(&nested, NULL, 8) == NULL && nested._errno == ENOMEM);
    CHECK(yields == before_yield);
    CHECK(os32_gui_retry_stats().retry_refused_count == before_refused+2);
    if (serve_probe) {
        CHECK(os32_gui_trim_serve(99,NULL) == 0 && done_calls == 0);
    }
}
static void *map_secondary(void *opaque, size_t bytes, unsigned flags)
{
    CHECK(opaque == &maps);
    callback_checks();
    attempts++; last_flags=flags;
    CHECK(flags == 0 || flags == OS32_NANO_TOPDOWN);
    if (reject_map || bytes > MAP_CAPACITY) return NULL;
    for (unsigned i=0; i<MAP_SLOTS; i++) if (!mapped[i]) {
        mapped[i]=bytes; maps++;
        memset(secondary[i],0,bytes);
        return secondary[i];
    }
    return NULL;
}
/* Executed before prefix loads: an early read of a foreign pointer must
 * fail a named assertion rather than count a host crash as mutation RED. */
void os32_nano_prefix_check(const void *prefix)
{
    CHECK(((uintptr_t)prefix & (OS32_NANO_PAGE-1u)) == 0);
    unsigned i;
    for (i=0;i<MAP_SLOTS;i++) if (prefix == secondary[i] && mapped[i]) break;
    CHECK(i < MAP_SLOTS);
}
static int unmap_secondary(void *opaque, uintptr_t base, size_t bytes)
{
    CHECK(opaque == &maps);
    callback_checks();
    CHECK(base != a.initial); /* Never return primary/BSS backing. */
    if (trim_fixture) {
        if (trim_callback_depth) return -1;
        trim_callback_depth=1;
        CHECK(os32_nano_trim() == 0);
        reent._errno=0;
        CHECK(malloc(8) == NULL && reent._errno == ENOMEM);
        trim_callback_depth=0;
        trim_unmaps++; trim_unmap_base=base; trim_unmap_bytes=bytes;
        CHECK(!(base & (OS32_NANO_PAGE-1u)) && bytes && !(bytes & (OS32_NANO_PAGE-1u)));
        if (base >= (uintptr_t)storage && base < (uintptr_t)storage+sizeof(storage)) {
            CHECK(base >= page_up(a.initial));
            CHECK(base+bytes == primary_end);
            if (reject_unmap && (!trim_fail_base || trim_fail_base==base)) return reject_unmap;
            primary_end=base;
            memset((void *)base,0xcc,bytes);
            return 0;
        }
        for (unsigned i=0;i<MAP_SLOTS;i++) {
            uintptr_t start=(uintptr_t)secondary[i];
            if (mapped[i] && base>=start && base<start+mapped[i]) {
                CHECK(base+bytes == start+mapped[i]);
                if (reject_unmap && (!trim_fail_base || trim_fail_base==base)) return reject_unmap;
                mapped[i]=base-start;
                unmaps++;
                memset((void *)base,0xcc,bytes);
                return 0;
            }
        }
        CHECK(0);
    }
    for (unsigned i=0; i<MAP_SLOTS; i++) if (base == (uintptr_t)secondary[i]) {
        CHECK(mapped[i] == bytes);
        if (reject_unmap) return -1;
        mapped[i]=0; unmaps++;
        memset((void *)base, 0xcc, bytes); /* No access to metadata after unmap. */
        return 0;
    }
    CHECK(0);
    return -1;
}
static int blocked_grow(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque; (void)base; (void)bytes;
    callback_checks();
    return -1;
}
static void automatic_tests(void)
{
    void *p, *q, *r, *keep;
    struct os32_nano_arena *other;
    unsigned saved;
    reset();
    a.grow_exact=blocked_grow;
    a.limit=UINTPTR_MAX;
    CHECK(os32_nano_configure(&a,map_secondary,unmap_secondary,&maps));
    keep=malloc(32); CHECK(keep); memset(keep,0x74,32);
    p=malloc(2*OS32_NANO_PAGE);
    CHECK(p && maps==1 && a.next && (uintptr_t)p >= a.next->initial);
    CHECK(a.next->map_base==(uintptr_t)secondary[0] && a.next->live==1);
    other=a.next;
    memset(p,0x6d,2*OS32_NANO_PAGE);
    CHECK(os32_nano_select(&a));
    free(p);
    CHECK(unmaps==1 && a.next==NULL && a.free_list==NULL && ((unsigned char*)keep)[31]==0x74);
    /* realloc first fails in primary, then succeeds in a separate arena. */
    p=realloc(keep,2*OS32_NANO_PAGE);
    CHECK(p && ((unsigned char*)p)[0]==0x74 && ((unsigned char*)p)[31]==0x74);
    CHECK(a.live==0 && a.next && a.next->live==1);
    other=a.next;
    memset(p,0x39,2*OS32_NANO_PAGE);
    reject_map=1;
    CHECK(realloc(p,8*OS32_NANO_PAGE)==NULL && ((unsigned char*)p)[8191]==0x39 && other->live==1);
    reject_map=0;
    q=realloc(p,8*OS32_NANO_PAGE);
    CHECK(q && ((unsigned char*)q)[8191]==0x39 && unmaps==2 && a.next!=other);
    other=a.next;
    /* Foreign pointers leave all arenas and live contents intact. */
    saved=unmaps;
    CHECK(os32_nano_select(&a));
    reent._errno=0;
    free(storage+sizeof(storage));
    CHECK(reent._errno==EINVAL && unmaps==saved && other->live==1);
    CHECK(realloc(storage+sizeof(storage),32)==NULL && reent._errno==EINVAL && other->live==1);
    /* Failed unmap restores reachability, allowing reuse and a later retry. */
    reject_unmap=1;
    free(q);
    CHECK(a.next==other && other->live==0 && unmaps==saved);
    reject_map=1;
    r=malloc(4*OS32_NANO_PAGE);
    CHECK(r && a.next==other && other->live==1 && maps==3);
    reject_map=reject_unmap=0;
    free(r);
    CHECK(a.next==NULL && unmaps==3);
    /* Two live arenas: freeing one must not touch its neighbor's state. */
    p=calloc(1,2*OS32_NANO_PAGE); q=malloc(8*OS32_NANO_PAGE);
    CHECK(p && q && ((unsigned char*)p)[8191]==0);
    memset(q,0x58,8*OS32_NANO_PAGE);
    CHECK(os32_nano_select(&a));
    free(p);
    CHECK(a.next && a.next->next==NULL && ((unsigned char*)q)[32767]==0x58);
    free(q);
    CHECK(!a.next && a.live==0);
    /* C classifies the requested size, independently of map overhead. */
    p=malloc(65535); CHECK(p && a.next && last_flags==0);
    free(p); CHECK(!a.next);
    p=malloc(65536); CHECK(p && !a.next && last_flags==OS32_NANO_TOPDOWN && !((uintptr_t)p&7));
    free(p); CHECK(!a.next);
    p=malloc(65537); CHECK(p && !a.next && last_flags==OS32_NANO_TOPDOWN);
    saved=unmaps; memset(p,0x71,65537);
    reject_map=1;
    CHECK(realloc(p,65535)==NULL && ((unsigned char*)p)[65536]==0x71 && unmaps==saved);
    CHECK(realloc(p,131072)==NULL && ((unsigned char*)p)[65536]==0x71 && unmaps==saved);
    reject_map=0;
    q=realloc(p,131072); CHECK(q && ((unsigned char*)q)[65536]==0x71 && unmaps==saved+1);
    p=realloc(q,64); CHECK(p && ((unsigned char*)p)[63]==0x71 && unmaps==saved+2);
    memset(p,0x72,64);
    reject_map=1; saved=unmaps;
    CHECK(realloc(p,65536)==NULL && ((unsigned char*)p)[63]==0x72 && unmaps==saved);
    reject_map=0;
    q=realloc(p,65536); CHECK(q && ((unsigned char*)q)[63]==0x72 && last_flags==OS32_NANO_TOPDOWN);
    saved=unmaps; reject_unmap=1;
    free(q); CHECK(unmaps==saved && ((unsigned char*)q)[63]==0x72);
    reject_unmap=0;
    free(q); CHECK(unmaps==saved+1);
    p=calloc(256,256); CHECK(p && last_flags==OS32_NANO_TOPDOWN);
    for (unsigned i=0;i<65536;i++) CHECK(((unsigned char*)p)[i]==0);
    saved=unmaps;
    /* A mapped foreign prefix is poisoned: no speculative prefix reads. */
    free((unsigned char*)p+8);
    CHECK(reent._errno==EINVAL && unmaps==saved && ((unsigned char*)p)[0]==0);
    free((void*)1);
    CHECK(reent._errno==EINVAL && unmaps==saved);
    free(p);
    saved=attempts;
    CHECK(_calloc_r(&reent,0x80000000u,2)==NULL && attempts==saved);
    CHECK(_calloc_r(&reent,0x80008000u,2)==NULL && attempts==saved);
    CHECK(_malloc_r(&reent,SIZE_MAX)==NULL && attempts==saved);
    CHECK(_malloc_r(&reent,SIZE_MAX-64u)==NULL && attempts==saved);
    p=malloc(65536); saved=unmaps;
    CHECK(realloc(p,0)==NULL && unmaps==saved+1);
    p=realloc(NULL,65536); CHECK(p && last_flags==OS32_NANO_TOPDOWN); free(p);
    p=malloc(16); saved=unmaps; free(p);
    CHECK(unmaps==saved && a.free_list && a.live==0);
}
/* f11: the range model rejects stale mapped_end and wrong EXACT restart. */
struct test_chunk { long size; struct test_chunk *next; };
static int trim_grow(void *opaque, uintptr_t base, size_t bytes)
{
    (void)opaque;
    callback_checks();
    exact_calls++; exact_base=base; exact_bytes=bytes;
    if (bytes>grow_ceiling) { exact_failures++; return -1; }
    if (base >= (uintptr_t)storage && base <= (uintptr_t)storage+sizeof(storage)) {
        CHECK(base == primary_end);
        if (base+bytes > (uintptr_t)storage+sizeof(storage)) { exact_failures++; return -1; }
        memset((void *)base,0,bytes); primary_end=base+bytes;
        return 0;
    }
    for (unsigned i=0;i<MAP_SLOTS;i++) if (mapped[i]) {
        uintptr_t start=(uintptr_t)secondary[i];
        if (base==start+mapped[i] && bytes<=MAP_CAPACITY-mapped[i]) {
            memset((void *)base,0,bytes); mapped[i]+=bytes;
            return 0;
        }
    }
    exact_failures++;
    return -1;
}
static void trim_reset(unsigned offset)
{
    CHECK(!a.next);
    for (unsigned i=0;i<MAP_SLOTS;i++) CHECK(!mapped[i]);
    reset();
    a.initial=a.brk=(uintptr_t)storage+offset;
    a.mapped_end=primary_end=(uintptr_t)storage+sizeof(storage);
    a.limit=UINTPTR_MAX; a.grow_exact=trim_grow;
    grow_ceiling=SIZE_MAX;
    trim_fixture=1; reject_map=reject_unmap=0;
    trim_unmaps=exact_calls=exact_failures=0;
    trim_fail_base=0;
}
static void same_bytes(const void *p, unsigned byte, size_t n)
{
    for (size_t i=0;i<n;i++) CHECK(((const unsigned char *)p)[i] == byte);
}
static void same_state(const struct os32_nano_arena *arena,
                       const struct os32_nano_arena *saved)
{
    CHECK(arena->free_list == saved->free_list && arena->brk == saved->brk &&
          arena->mapped_end == saved->mapped_end && arena->sbrk_start == saved->sbrk_start &&
          arena->initial == saved->initial && arena->next == saved->next && arena->live == saved->live);
    for (size_t i=0;i<sizeof(arena->info);i++)
        CHECK(((const unsigned char *)&arena->info)[i] == ((const unsigned char *)&saved->info)[i]);
}
static void trim_tests(void)
{
    void *p, *q, *hole, *again;
    struct test_chunk *tail;
    struct os32_nano_arena saved, *other;
    struct mallinfo before, after;
    uintptr_t keep, end;
    long old_size;
    unsigned calls_before;

    /* Standalone tests leave arenas unconfigured; automatic tests configure. */
    CHECK(os32_nano_trim() == 0);
    automatic_tests();
    trim_reset(0);
    p=malloc(128); CHECK(p); memset(p,0x61,128);
    q=malloc(2*OS32_NANO_PAGE); CHECK(q);
    saved=a;
    CHECK(os32_nano_trim() == 0 && trim_unmaps == 0); /* USED tail. */
    same_state(&a,&saved); same_bytes(p,0x61,128);
    free(p); saved=a; /* Free hole is not a tail: do not return live q. */
    memset(q,0x62,2*OS32_NANO_PAGE);
    CHECK(os32_nano_trim() == 0 && trim_unmaps == 0);
    same_state(&a,&saved); same_bytes(q,0x62,2*OS32_NANO_PAGE);
    free(q);

    trim_reset(0);
    hole=malloc(32); p=malloc(128); q=malloc(2*OS32_NANO_PAGE);
    CHECK(hole && p && q); memset(p,0x63,128);
    free(hole); free(q);
    tail=((struct test_chunk *)a.free_list)->next;
    CHECK(tail && tail->next==NULL);
    keep=page_up((uintptr_t)tail+12u); end=a.mapped_end;
    before=os32_nano_info(&reent); saved=a; old_size=tail->size;
    /* EFULL: 中抜きで slot 不足. The callback returns the public error code;
     * extent splitting itself is covered by appmem's integration fixture. */
    const int failures[]={-13, -9}; /* OS32_ERR_FULL, OS32_ERR_INVAL */
    for (unsigned i=0;i<2;i++) {
        reject_unmap=failures[i];
        CHECK(os32_nano_trim() == 0);
        CHECK(tail->size == old_size && a.brk == saved.brk && a.mapped_end == saved.mapped_end);
        CHECK(tail->next == NULL && ((struct test_chunk *)a.free_list)->next == tail);
        same_state(&a,&saved);
        after=os32_nano_info(&reent);
        CHECK(after.arena == before.arena && after.fordblks == before.fordblks);
        same_bytes(p,0x63,128);
    }
    reject_unmap=0;
    CHECK(os32_nano_trim() == (end-keep)/OS32_NANO_PAGE);
    CHECK(a.free_list == saved.free_list && ((struct test_chunk *)a.free_list)->next == tail);
    CHECK(tail->size == (long)(keep-(uintptr_t)tail) && a.brk == keep && a.mapped_end == keep);
    after=os32_nano_info(&reent);
    CHECK(after.arena == keep-(uintptr_t)a.sbrk_start &&
          after.fordblks == before.fordblks-old_size+(size_t)tail->size);
    same_bytes(p,0x63,128);
    saved=a; calls_before=trim_unmaps;
    CHECK(os32_nano_trim() == 0 && trim_unmaps == calls_before);
    same_state(&a,&saved);
    /* Full nano request needs two pages; only the one-page difference fits.
     * This must reach nano-mallocr.c:326-352 and unlink the retained tail. */
    grow_ceiling=OS32_NANO_PAGE;
    again=malloc(OS32_NANO_PAGE+512);
    CHECK(again == q && exact_failures == 1 && exact_calls == 2);
    CHECK(exact_base == keep && exact_bytes == OS32_NANO_PAGE && a.mapped_end == keep+OS32_NANO_PAGE);
    CHECK(((struct test_chunk *)a.free_list)->next == NULL &&
          a.brk == (uintptr_t)tail+(uintptr_t)tail->size);
    same_bytes(p,0x63,128);
    free(again); free(p);

    /* Page-start, middle (above), and near-end headers; initial image page. */
    const unsigned offsets[]={0, OS32_NANO_PAGE-8, 123};
    for (unsigned i=0;i<3;i++) {
        trim_reset(offsets[i]);
        p=malloc(OS32_NANO_PAGE); CHECK(p); free(p);
        tail=a.free_list; keep=page_up((uintptr_t)tail+12u); end=a.mapped_end;
        CHECK(keep >= page_up(a.initial));
        CHECK(os32_nano_trim() == (end-keep)/OS32_NANO_PAGE);
        CHECK(a.brk == keep && a.mapped_end == keep && tail->size == (long)(keep-(uintptr_t)tail));
        CHECK(tail->size >= 12 && tail->next == NULL);
    }
    trim_reset(0);
    p=malloc(OS32_NANO_PAGE); CHECK(p); free(p);
    end=a.brk;
    CHECK(os32_nano_morecore(&a,&reent,64) == (void *)end);
    saved=a; old_size=((struct test_chunk *)a.free_list)->size;
    CHECK(os32_nano_trim() == 0 && trim_unmaps == 0); /* Direct sbrk gap. */
    same_state(&a,&saved);
    CHECK(((struct test_chunk *)a.free_list)->size == old_size);

    trim_reset(0);
    a.grow_exact=blocked_grow;
    p=malloc(2*OS32_NANO_PAGE); CHECK(p); /* live primary */
    q=malloc(2*OS32_NANO_PAGE); CHECK(q && a.next);
    other=a.next; other->grow_exact=trim_grow;
    memset(q,0x64,2*OS32_NANO_PAGE);
    hole=malloc(8*OS32_NANO_PAGE); CHECK(hole && a.next==other && other->live==2);
    free(hole); tail=other->free_list;
    keep=page_up((uintptr_t)tail+12u); end=other->mapped_end;
    CHECK(os32_nano_select(&a)); /* Nonselected arena uses its saved free list. */
    struct test_chunk *poisoned=(struct test_chunk *)other->initial;
    long live_size=poisoned->size; poisoned->size=0;
    CHECK(os32_nano_trim() == (end-keep)/OS32_NANO_PAGE);
    CHECK(other->brk == keep && other->mapped_end == keep &&
          other->map_base+OS32_NANO_PAGE == other->initial);
    CHECK(poisoned->size == 0);
    poisoned->size=live_size;
    same_bytes(q,0x64,2*OS32_NANO_PAGE);
    free(q);
    CHECK(!a.next && trim_unmap_base == (uintptr_t)secondary[0] &&
          trim_unmap_bytes == keep-(uintptr_t)secondary[0]);
    free(p);

    /* Empty secondary retained after free/unmap failure, retried by trim. */
    trim_reset(0); a.grow_exact=blocked_grow;
    p=malloc(8*OS32_NANO_PAGE); CHECK(p && a.next); other=a.next;
    end=other->mapped_end-other->map_base;
    reject_unmap=-9; free(p);
    CHECK(a.next == other && other->live == 0);
    reject_unmap=0;
    CHECK(os32_nano_trim() == end/OS32_NANO_PAGE && !a.next);

    /* Independent transactions and sum, with/without primary failure. */
    for (unsigned fail=0;fail<2;fail++) {
        trim_reset(0); a.grow_exact=blocked_grow;
        p=malloc(2*OS32_NANO_PAGE); q=malloc(2*OS32_NANO_PAGE);
        CHECK(p && q && a.next); other=a.next; other->grow_exact=trim_grow;
        hole=malloc(8*OS32_NANO_PAGE); CHECK(hole); free(hole); free(p);
        tail=a.free_list; old_size=tail->size; saved=a;
        keep=page_up((uintptr_t)other->free_list+12u); end=other->mapped_end;
        trim_fail_base=page_up((uintptr_t)tail+12u); reject_unmap=fail ? -9 : 0;
        size_t primary_pages=fail ? 0 : (a.mapped_end-trim_fail_base)/OS32_NANO_PAGE;
        CHECK(os32_nano_trim() == primary_pages+(end-keep)/OS32_NANO_PAGE && trim_unmaps == 2);
        if (fail) { same_state(&a,&saved); CHECK(tail->size == old_size); }
        else CHECK(a.brk == trim_fail_base && a.mapped_end == trim_fail_base);
        CHECK(other->mapped_end == keep && trim_unmap_base == keep);
        reject_unmap=0; free(q);
    }

    /* No live header reads, even for a nonselected arena. Poison size so a
     * physical-chunk scan would mistake its live first chunk for a free tail. */
    trim_reset(0); a.grow_exact=blocked_grow;
    p=malloc(2*OS32_NANO_PAGE); q=malloc(2*OS32_NANO_PAGE);
    CHECK(p && q && a.next); other=a.next;
    memset(q,0x65,2*OS32_NANO_PAGE);
    CHECK(os32_nano_select(&a));
    saved=*other;
    struct test_chunk *live=(struct test_chunk *)other->initial;
    old_size=live->size; live->size=12;
    CHECK(os32_nano_trim() == 0 && trim_unmaps == 0);
    same_state(other,&saved);
    CHECK(live->size == 12);
    same_bytes(q,0x65,2*OS32_NANO_PAGE);
    live->size=old_size; free(q); free(p);

    /* One connected sequence: hole + alignment, blocked EXACT, routing,
     * cross-arena realloc, trim and reuse of the retained primary tail. */
    trim_reset(1); a.mapped_end=primary_end=(uintptr_t)storage+OS32_NANO_PAGE;
    a.grow_exact=blocked_grow;
    hole=malloc(32); p=malloc(64); CHECK(hole && p && !((uintptr_t)p&7u));
    memset(p,0x66,64); free(hole); CHECK(malloc(32)==hole);
    q=realloc(p,2*OS32_NANO_PAGE); CHECK(q && a.next); same_bytes(q,0x66,64);
    other=a.next; other->grow_exact=trim_grow;
    p=malloc(8*OS32_NANO_PAGE); CHECK(p); free(p);
    CHECK(os32_nano_trim() >= 1);
    p=malloc(8*OS32_NANO_PAGE); CHECK(p && a.next==other);
    same_bytes(q,0x66,64); free(p); free(q); free(hole);
}

static int recover_on_yield;
static void *retry_old;
static i32 retry_yield(void)
{
    yields++;
    CHECK(yields <= 1);
    if (retry_old) {
        same_bytes(retry_old,0x79,65536);
        CHECK(mapped[0] != 0); /* First failure retained the old allocation. */
    }
    if (recover_on_yield) reject_map=0;
    return 0;
}
static int retry_done(u32 op, u32 epoch)
{
    CHECK(op == GUI_OP_TRIM_DONE && epoch == 77);
    CHECK(done_calls == 0 && serve_probe == 2);
    done_calls++;
    return 0;
}
static uint32_t retry_hook(void)
{
    CHECK(serve_probe == 1 && trim_unmaps == 1 && done_calls == 0);
    CHECK(os32_gui_trim_serve(77,NULL) == 0 && done_calls == 0);
    unsigned before=yields;
    reject_map=1;
    CHECK(malloc(65536) == NULL && yields == before);
    serve_probe=2;
    return 3;
}
static void retry_tests(void)
{
    void *p, *q;
    struct os32_gui_retry_stats before, after;
    mock_api.sys_yield=retry_yield;
    mock_api.gui_call=retry_done;
    trim_reset(0); reject_map=1; yields=0;
    CHECK(malloc(65536) == NULL && yields == 0); /* GUI flag false, resident equivalent. */
    os32_gui_retry_enable();
    before=os32_gui_retry_stats(); recover_on_yield=1;
    p=malloc(65536);
    CHECK(p != NULL && yields == 1);
    after=os32_gui_retry_stats();
    CHECK(after.retry_count == before.retry_count+1 && after.retry_ok_count == before.retry_ok_count+1);
    free(p);
    trim_reset(0); reject_map=1; yields=0; recover_on_yield=0;
    before=os32_gui_retry_stats();
    CHECK(malloc(65536) == NULL && yields == 1);
    after=os32_gui_retry_stats();
    CHECK(after.retry_count == before.retry_count+1 && after.retry_ok_count == before.retry_ok_count);
    yields=0;
    /* Base 485f4a9: nano returns freeable, non-NULL zero-size allocations. */
    void *zero[3] = {malloc(0), calloc(0,8), calloc(8,0)};
    CHECK(zero[0] != NULL && zero[1] != NULL && zero[2] != NULL && yields == 0);
    free(zero[0]); free(zero[1]); free(zero[2]);
    CHECK(yields == 0);
    reset(); a.limit=a.brk; /* No free chunks or growth: size-zero allocation fails. */
    yields=0;
    CHECK(calloc(0,8) == NULL && yields == 0);
    trim_reset(0); reject_map=1;
    CHECK(_calloc_r(&reent,0x80000000u,2) == NULL && yields == 0);
    CHECK(os32_nano_select(NULL));
    CHECK(malloc(8) == NULL && yields == 0);
    CHECK(os32_nano_select(&a));
    unsigned old_attempts=attempts;
    CHECK(os32_nano_sbrk(&reent,16) == (void *)-1 && yields == 0 && attempts == old_attempts);
    reject_map=0;
    p=malloc(65536); CHECK(p); memset(p,0x79,65536);
    retry_old=p; reject_map=1; yields=0; recover_on_yield=1;
    unsigned old_unmaps=unmaps;
    q=realloc(p,131072);
    CHECK(q != NULL && yields == 1);
    same_bytes(q,0x79,65536);
    CHECK(unmaps == old_unmaps+1 && mapped[0] == 0);
    retry_old=NULL; free(q);
    p=malloc(65536); CHECK(p); memset(p,0x79,65536);
    reject_map=1; yields=0; recover_on_yield=0; retry_old=p;
    CHECK(realloc(p,131072) == NULL && yields == 1);
    same_bytes(p,0x79,65536); retry_old=NULL;
    yields=0; old_unmaps=unmaps;
    CHECK(realloc(p,0) == NULL && yields == 0 && unmaps == old_unmaps+1);
    reject_map=1; yields=0; recover_on_yield=1;
    p=calloc(256,256); CHECK(p != NULL && yields == 1);
    same_bytes(p,0,65536); free(p);
    trim_reset(0); p=malloc(8192); CHECK(p); free(p);
    done_calls=yields=0; serve_probe=1;
    before=os32_gui_retry_stats();
    CHECK(os32_gui_trim_serve(77,retry_hook) == 3);
    after=os32_gui_retry_stats();
    CHECK(done_calls == 1 && yields == 0 && after.trim_serve_count == before.trim_serve_count+1);
    CHECK(after.hook_pages_total == before.hook_pages_total+3);
    serve_probe=0;
}

void _start(void)
{
    tests(); trim_tests(); retry_tests(); write_text("nano adapter: ",14); number(checks); write_text(" checks GREEN\n",14); stop(0);
}
