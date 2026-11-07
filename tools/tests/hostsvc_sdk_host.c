/* SDK unit: real transport/LIST parser, fake nonblocking KAPI, libc-free ILP32. */
#include "os32api.h"
#include "libos32host.h"
#ifndef PRODUCT_SOURCE
#define PRODUCT_SOURCE "../../userland/lib/host/host_call.c"
#endif
static KernelAPI table;
KernelAPI *kapi = &table;
#include PRODUCT_SOURCE

static u32 now, closes, opens, yields, off, wrote, open_wait, status_wait;
static u32 read_wait, write_wait, step, generation, service_id, alive_id;
static int silence, terminal, notice_stale, alive_ready, notice_rc, list_calls, fail_alloc;
static const char *body;
static u32 body_len, chunk;
static char request_line[HOST_REQ_MAX + 1];
static u8 arena[65536];
static u32 arena_used, freed;
static int eq(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static void *alloc(u32 n)
{
    u32 aligned = (n + 3) & ~3u;
    void *p;
    if (fail_alloc || aligned > sizeof(arena) - arena_used) return 0;
    p = arena + arena_used; arena_used += aligned; return p;
}
static void release(void *p) { if (p) freed++; }
static u32 tick(void) { return now; }
static i32 yield(void) { now += step; yields++; return 0; }
static i32 open_call(const char *req, u32 n)
{
    opens++;
    if (terminal) return terminal;
    if (open_wait) { open_wait--; return OS32_ERR_FULL; }
    for (u32 i = 0; i < n; i++) request_line[i] = req[i];
    request_line[n] = 0;
    if (eq(request_line, "LIST")) {
        static const char list1[] = "generation 7\nos32.alive 1 8 builtin\ncustom.name 1 42 plugin\nother 1 43 plugin\n";
        static const char list2[] = "generation 9\nos32.alive 1 10 builtin\ncustom.name 1 51 plugin\nother 1 52 plugin\n";
        body = generation == 7 ? list1 : list2;
        body_len = 0; while (body[body_len]) body_len++;
        list_calls++;
    }
    off = wrote = 0;
    return 3;
}
static i32 status_call(i32 h, u32 *st, u32 *len)
{
    (void)h;
    /* Real KAPI returns HOSTDOWN only after its last peer response ages out. */
    if (terminal) return terminal;
    if (silence) return now >= 100 ? OS32_ERR_HOSTDOWN : OS32_ERR_AGAIN;
    if (status_wait) { status_wait--; return OS32_ERR_AGAIN; }
    *st = 200; *len = body_len; return 0;
}
static i32 read_call(i32 h, void *out, u32 cap)
{
    u8 *dst = out;
    u32 n = body_len - off;
    (void)h;
    if (read_wait) { read_wait--; return OS32_ERR_AGAIN; }
    if (n > chunk) n = chunk;
    if (n > cap) n = cap;
    for (u32 i = 0; i < n; i++) dst[i] = (u8)body[off + i];
    off += n; return (i32)n;
}
static i32 write_call(i32 h, const void *in, u32 n)
{
    const u8 *src = in;
    (void)h;
    if (write_wait) { write_wait--; return OS32_ERR_AGAIN; }
    if (n > 2) n = 2;
    for (u32 i = 0; i < n; i++) if (src[i] != (u8)('a' + wrote + i)) return OS32_ERR_IO;
    wrote += n; return (i32)n;
}
static i32 close_call(i32 h) { if (h == 3) closes++; return 0; }
static int notice(u32 gen, u32 service, HostNotice *out)
{
    if (notice_stale) { generation = 9; service_id = 51; alive_id = 10; notice_stale = 0; }
    if (gen != generation) return OS32_ERR_STALE;
    if (service == alive_id) {
        if (!alive_ready) return OS32_ERR_AGAIN;
        alive_ready = 0;
        *out = (HostNotice){ .host_generation = generation, .service = (u16)alive_id,
                             .serial = 77, .kind = 1, .length = 4 };
        return 0;
    }
    if (service != service_id) return OS32_ERR_IO;
    if (notice_rc) return notice_rc;
    *out = (HostNotice){ .host_generation = generation, .service = (u16)service_id, .serial = 90 };
    return 0;
}
static void reset(void)
{
    notice_clear(); have_alive = 0;
    now = opens = closes = yields = off = wrote = 0;
    open_wait = status_wait = read_wait = write_wait = 0;
    step = 1; silence = 0; terminal = notice_stale = alive_ready = notice_rc = 0;
    list_calls = fail_alloc = 0;
    body = "xyz"; body_len = 3; chunk = 2;
    generation = 7; service_id = 42; alive_id = 8;
    arena_used = freed = 0;
}
#define REQUIRE(x) do { if (!(x)) return __LINE__; } while (0)
static int suite(void)
{
    char out[8];
    HostNotice n;
    table = (KernelAPI){ .version = 82, .get_tick = tick, .sys_yield = yield,
        .host_open = open_call, .host_status = status_call, .host_read = read_call,
        .host_write = write_call, .host_close = close_call, .host_notice_read = notice,
        .mem_alloc = alloc, .mem_free = release };
    reset();
    open_wait = status_wait = read_wait = write_wait = 1;
    REQUIRE(host_call("ECHO 5", "abcde", 5, out, sizeof(out), 1000) == 3);
    REQUIRE(wrote == 5 && eq(request_line, "ECHO 5") && out[0] == 'x' && out[2] == 'z');
    REQUIRE(closes == 1 && yields == 4);
    reset(); REQUIRE(host_call("TIME", 0, 0, out, 2, 1000) == OS32_ERR_NOSPC && closes == 1);
    reset(); REQUIRE(host_call("TIME", 0, 0, 0, 0, 1000) == OS32_ERR_NOSPC);
    reset(); terminal = OS32_ERR_HOSTDOWN;
    REQUIRE(host_call("TIME", 0, 0, out, 8, 1000) == OS32_ERR_HOSTDOWN && !closes && !yields);
    reset(); silence = 1;
    REQUIRE(host_call("TIME", 0, 0, out, 8, 2000) == OS32_ERR_HOSTDOWN && now == 100 && closes == 1);
    reset(); silence = 1;
    REQUIRE(host_call("TIME", 0, 0, out, 8, 500) == OS32_ERR_TIMEOUT && now == 50);
    reset(); status_wait = 200; REQUIRE(host_call("TIME", 0, 0, out, 8, 25) == OS32_ERR_TIMEOUT);
    REQUIRE(now == 3 && closes == 1);
    reset(); now = 0xfffffffeu; status_wait = 200;
    REQUIRE(host_call("TIME", 0, 0, out, 8, 30) == OS32_ERR_TIMEOUT && now == 1);
    reset(); status_wait = 2; read_wait = 2; write_wait = 2;
    REQUIRE(host_call("ECHO 5", "abcde", 5, out, 8, 50) == OS32_ERR_TIMEOUT && wrote == 5);
    reset(); status_wait = 120; /* >1s of PROCESSING, KAPI remains alive */
    REQUIRE(host_call("TIME", 0, 0, out, 8, 2000) == 3 && now == 120);
    reset(); REQUIRE(host_call("TIME", 0, 0, out, 8, 0) == 3 && !yields);
    reset(); status_wait = 1;
    REQUIRE(host_call("TIME", 0, 0, out, 8, 0) == OS32_ERR_AGAIN && !yields && closes == 1);
    reset(); open_wait = 1;
    REQUIRE(host_call("TIME", 0, 0, out, 8, 0) == OS32_ERR_AGAIN && !closes && !yields);
    reset(); REQUIRE(host_call(0, 0, 0, out, 8, 1) == OS32_ERR_INVAL);
    REQUIRE(host_call("", 0, 0, out, 8, 1) == OS32_ERR_INVAL);
    REQUIRE(host_call("TIME", 0, 1, out, 8, 1) == OS32_ERR_INVAL);
    REQUIRE(host_call("TIME", 0, 0, 0, 8, 1) == OS32_ERR_INVAL);
    reset(); REQUIRE(os32_host_notice_read("custom.name", &n) == 0 && n.service == 42);
    REQUIRE(os32_host_notice_read("custom.name", &n) == 0 && list_calls == 1);
    REQUIRE(os32_host_notice_read("unknown", &n) == OS32_ERR_NOTFOUND);
    notice_rc = OS32_ERR_AGAIN;
    REQUIRE(os32_host_notice_read("custom.name", &n) == OS32_ERR_AGAIN && list_calls == 1);
    notice_rc = 0; notice_stale = 1;
    REQUIRE(os32_host_notice_read("custom.name", &n) == 0 && n.service == 51 && list_calls == 2);
    alive_ready = 1;
    REQUIRE(os32_host_notice_read("custom.name", &n) == 0 && list_calls == 3);
    REQUIRE(os32_host_notice_read("os32.alive", &n) == 0 && n.serial == 77 && list_calls == 3);
    REQUIRE(freed > 0);
    reset(); fail_alloc = 1;
    REQUIRE(os32_host_notice_read("custom.name", &n) == OS32_ERR_NOSPC && closes == 1);
    reset(); table.version = 81;
    REQUIRE(os32_host_notice_read("custom.name", &n) == OS32_ERR_NOSYS && opens == 0);
    return 0;
}
void _start(void)
{
    int rc = suite();
    /* Linux exit is only 8 bits; retain a nonzero line on every failure. */
    if (rc) rc = rc % 254 + 1;
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory");
    __builtin_unreachable();
}
