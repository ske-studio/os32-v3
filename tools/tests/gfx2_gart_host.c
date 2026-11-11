#include "gfx2_int.h"
static u32 owners[128], pins[128], next, calls, fail_alloc, fail_pin;
static int bind_rc, unbind_rc, transfers, seq, unbind_at, unpin_at, free_at;
u32 gfx2_quar_owner = 9;
int pgalloc_alloc_n_owner(u32 owner, int n, u32 first, u32 end, u32 flags, u32 *pfn)
{
    (void)n; (void)first; (void)end; (void)flags;
    if (++calls == fail_alloc) return 0;
    next += 2; *pfn = next; owners[next] = owner; return 1;
}
int pgalloc_free_n_owner(u32 owner, u32 pfn, int n)
{
    (void)n; free_at = ++seq;
    if (owners[pfn] != owner || pins[pfn]) return 0;
    owners[pfn] = 0; return 1;
}
int pgalloc_pin(u32 pfn, u32 n, u32 owner)
{
    (void)n;
    if (pfn == fail_pin || owners[pfn] != owner) return 0;
    pins[pfn]++; return 1;
}
int pgalloc_unpin(u32 pfn, u32 n, u32 owner)
{
    (void)n; unpin_at = ++seq;
    if (owners[pfn] != owner || !pins[pfn]) return 0;
    pins[pfn]--; return 1;
}
int pgalloc_page_owned(u32 pfn, u32 owner) { return owners[pfn] == owner; }
int pgalloc_pinned(u32 pfn) { return pins[pfn]; }
int ledger_transfer_pinned(u32 pfn, int n, u32 from, u32 to)
{
    (void)n;
    if (owners[pfn] != from || !pins[pfn]) return 0;
    transfers++; owners[pfn] = to; return 1;
}
static int bind(void *ctx, const u32 *p, u32 n, u32 *off)
{ (void)ctx; (void)p; (void)n; *off = 123; return bind_rc; }
static int unbind(void *ctx, u32 off, u32 n)
{ (void)ctx; (void)off; (void)n; unbind_at = ++seq; return unbind_rc; }
static int test(void)
{
    static const GfxBackendV2 be = { .gart_bind = bind, .gart_unbind = unbind };
    static struct gfx2_adapter a;
    static struct gfx2_surf s;
    const u32 *p; u32 n, p0, p1, p2;
    a.be = &be; s.bytes = 3 * MEM_PAGE_SIZE; s.pool = GFX2_POOL_GART;
    fail_alloc = 3;
    if (gfx2_gart_alloc(&a, &s, 3) != OS32_ERR_NOSPC || owners[2] || owners[4]) return 1;
    fail_alloc = 0; calls = 0; bind_rc = OS32_ERR_NOSPC; next = 0;
    if (gfx2_gart_alloc(&a, &s, 3) != OS32_ERR_NOSPC || owners[2] || owners[4] || owners[6] || pins[2] || pins[4] || pins[6]) return 2;
    bind_rc = 0; next = 0; calls = 0; fail_pin = 4;
    if (gfx2_gart_alloc(&a, &s, 3) != OS32_ERR_IO || owners[2] || pins[2]) return 3;
    fail_pin = 0; next = 0; calls = 0;
    if (gfx2_gart_alloc(&a, &s, 3) || gfx2_gart_pfns(&s, &p, &n) || n != 3) return 4;
    p0 = p[0]; p1 = p[1]; p2 = p[2];
    a.state = GFX2_AD_LOST_HELD; seq = 0;
    if (gfx2_gart_free(&a, &s) != OS32_ERR_BUSY || seq) return 5;
    a.state = 0; unbind_rc = OS32_ERR_INVAL;
    if (gfx2_gart_free(&a, &s) != OS32_ERR_IO || !pins[p0] || owners[p0] != 3) return 6;
    unbind_rc = 0; pins[p1] = 0; transfers = 0;
    if (gfx2_gart_quarantine(&s) != OS32_ERR_IO || transfers || owners[p0] != 3) return 7;
    pins[p1] = 1;
    if (gfx2_gart_quarantine(&s) || transfers != 3 || s.gart_moved != 3 || owners[p2] != 9) return 8;
    seq = 0; unbind_at = unpin_at = free_at = 0;
    if (gfx2_gart_free(&a, &s) || owners[p0] || owners[p1] || owners[p2] || !(unbind_at < unpin_at && unpin_at < free_at)) return 9;
    s.state = GFX2_ST_QUAR;
    s.bytes = MEM_PAGE_SIZE; next = 0; calls = 0;
    if (gfx2_gart_alloc(&a, &s, 3)) return 10;
    gfx2_quar_owner = 0;
    if (gfx2_gart_quarantine(&s) != OS32_ERR_IO || gfx2_gart_free(&a, &s) || owners[2] != 3 || pins[2] != 1) return 12;
    s.bytes = 0;
    if (gfx2_gart_alloc(&a, &s, 3) != OS32_ERR_INVAL) return 10;
    s.bytes = 8192u * 1024u + 1;
    if (gfx2_gart_alloc(&a, &s, 3) != OS32_ERR_NOSPC) return 11;
    return 0;
}
void _start(void) { int rc = test(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc)); __builtin_unreachable(); }
