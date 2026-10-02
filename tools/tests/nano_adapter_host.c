/* Real nano objects, freestanding ILP32 Linux entry; no host libc allocator. */
#include "nano_adapter.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>
extern void *_reallocf_r(struct _reent *, void *, size_t);
static struct _reent reent;
struct _reent *_impure_ptr = &reent;
static unsigned char storage[4 * OS32_NANO_PAGE] __attribute__((aligned(OS32_NANO_PAGE)));
static struct os32_nano_arena a, b;
static int calls, fail_call, reenter, reentry_ok;
static uintptr_t map_base;
static size_t map_bytes;
static int checks;
void *memset(void *p, int c, size_t n) { unsigned char *q=p; while(n--) *q++=c; return p; }
void *memcpy(void *p, const void *s, size_t n) { unsigned char *q=p; const unsigned char *r=s; while(n--) *q++=*r++; return p; }
static void write_text(const char *s, size_t n)
{
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
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
void _start(void)
{
    tests(); write_text("nano adapter: ",14); number(checks); write_text(" checks GREEN\n",14); stop(0);
}
