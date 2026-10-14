/* Real CRT reader/formatter and bootlog storage, ILP32 without libc.
 * 記録: tools/tests/crt_diag_tdd.md */
#define BOOTLOG_NO_IRQ_LOCK
#include "../../kernel/bootlog.c"
#include "memmap.h"

#define FAKE_SIZE 0x100000
#define EVENT_MAX 64
static u8 fake[FAKE_SIZE] __attribute__((aligned(4096)));
static u32 mem_addr[EVENT_MAX];
static int mem_io[EVENT_MAX], mem_n;
static void *fake_map(u32 pa, int io);
#define P2V(pa) fake_map((pa), 0)
#define P2V_IO(pa) fake_map((pa), 1)
#include "../../kdebug/crt_diag.c"

static void output(const char *s)
{
    unsigned int n = 0;
    while (s[n]) n++;
    int rc;
    __asm__ volatile("int $0x80" : "=a"(rc)
                     : "0"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(c) do { if (!(c)) { output("FAIL " #c "\n"); finish(1); } } while (0)

static int equal(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return *a == *b;
}
static int length(const char *s)
{
    int n = 0;
    while (s[n]) n++;
    return n;
}

/* Independent address/value oracle, deliberately not taken from crt_diag.h. */
static const u32 work[] = { 0x400, 0x402, 0x459, 0x53C, 0x54C, 0x54D };
static const char expected[] =
    "[crt] 09a8=81 sysa=5a w0400=10 w0402=11 w0459=12 w053c=13 w054c=14 w054d=15 "
    "msw=a3fe2:20,a3fe4:21,a3fe6:22,a3fe8:23,a3fea:24,a3fec:25,a3fee:26,"
    "a3ff0:27,a3ff2:28,a3ff4:29,a3ff6:2a,a3ff8:2b,a3ffa:2c,a3ffc:2d,a3ffe:2e(15)\n"
    "[crt2] sdip_bank=unknown sdip=841e:30,851e:31,861e:32,871e:33,881e:34,"
    "891e:35,8a1e:36,8b1e:37,8c1e:38,8d1e:39,8e1e:3a,8f1e:3b(12)";

static int ev_n, out_n, all_ff;
static unsigned int ev_port[EVENT_MAX], ev_value[EVENT_MAX];
static int ev_width[EVENT_MAX], ev_out[EVENT_MAX];
static void event(unsigned int port, unsigned int value, int width, int out)
{
    CHECK(ev_n < EVENT_MAX);
    ev_port[ev_n] = port; ev_value[ev_n] = value;
    ev_width[ev_n] = width; ev_out[ev_n++] = out;
    out_n += out;
}
unsigned int crt_shim_in(unsigned int port, int width)
{
    unsigned int value = 0;
    if (port == 0x9A8) value = 0x81;
    else if (port == 0x31) value = 0x5A;
    else if (port >= 0x841E && port <= 0x8F1E && (port & 0xFF) == 0x1E)
        value = 0x30 + (port - 0x841E) / 0x100;
    else CHECK(0);
    if (all_ff) value = 0xFF;
    event(port, value, width, 0);
    return value;
}
void crt_shim_out(unsigned int port, unsigned int value, int width)
{ event(port, value, width, 1); }
static void *fake_map(u32 pa, int io)
{
    CHECK(pa < FAKE_SIZE && mem_n < EVENT_MAX);
    mem_addr[mem_n] = pa; mem_io[mem_n++] = io;
    return &fake[pa];
}
static void reset_events(void) { ev_n = out_n = mem_n = 0; }
static void check_events(void)
{
    CHECK(out_n == 0 && ev_n == 14 && mem_n == 21);
    CHECK(ev_port[0] == 0x9A8 && ev_port[1] == 0x31);
    for (int i = 0; i < 14; i++) CHECK(ev_width[i] == 8 && !ev_out[i]);
    for (int i = 0; i < 12; i++) CHECK(ev_port[i + 2] == 0x841EU + 0x100U * i);
    for (int i = 0; i < 6; i++) CHECK(mem_addr[i] == work[i] && mem_io[i] == 0);
    for (int i = 0; i < 15; i++)
        CHECK(mem_addr[i + 6] == 0xA3FE2U + 2U * i && mem_io[i + 6] == 1);
}

unsigned int bootlog_lock(void) { return 0; }
void bootlog_unlock(unsigned int flags) { CHECK(flags == 0); }
static int print_n;
void __cdecl kprintf(u8 attr, const char *fmt, ...)
{
    __builtin_va_list args;
    CHECK(attr == 0x07 && equal(fmt, "%s\n"));
    __builtin_va_start(args, fmt);
    const char *s = __builtin_va_arg(args, const char *);
    __builtin_va_end(args);
    print_n++;
    /* Match console's byte capture; bootlog_compose must retain both lines. */
    while (*s) bootlog_push(s++, 1);
    bootlog_push("\n", 1);
}

static void protect_fake(void)
{
    int rc;
    /* i386 mprotect, PROT_READ: even a store of the same value must fault. */
    __asm__ volatile("int $0x80" : "=a"(rc)
                     : "0"(125), "b"(fake), "c"(FAKE_SIZE), "d"(1) : "memory");
    CHECK(rc == 0);
}

void _start(void)
{
    CrtDiagRaw raw, saved;
    char buf[CRT_DIAG_BUFSZ], again[CRT_DIAG_BUFSZ];
    int n = length(expected);
    for (int i = 0; i < FAKE_SIZE; i++) fake[i] = (u8)(i * 17 + i / 256 + 3);
    for (int i = 0; i < 6; i++) fake[work[i]] = (u8)(0x10 + i);
    for (int i = 0; i < 15; i++) fake[0xA3FE2 + 2 * i] = (u8)(0x20 + i);
    protect_fake();

    /* Prove the shim records word I/O and io_wait, before resetting it. */
    (void)inp(0x9A8); (void)inpw(0x31);
    outp(0x68, 0x0D); outpw(0x8F1F, 0x1234); io_wait();
    CHECK(ev_n == 5 && out_n == 3);
    CHECK(ev_width[1] == 16 && ev_value[3] == 0x1234 && ev_width[3] == 16);
    CHECK(ev_out[4] && ev_port[4] == 0x5F);
    reset_events();
    for (unsigned int i = 0; i < sizeof(raw); i++) ((u8 *)&raw)[i] = 0xCC;
    crt_diag_read(&raw);
    check_events();
    CHECK(raw.hsync == 0x81 && raw.sysa == 0x5A);
    for (int i = 0; i < 6; i++) CHECK(raw.w[i] == 0x10 + i);
    for (int i = 0; i < 15; i++) CHECK(raw.msw[i] == 0x20 + i);
    for (int i = 0; i < 12; i++) CHECK(raw.sdip[i] == 0x30 + i);
    output("PASS read: zero OUT, 14 IN, 21 mapped bytes, read-only memory\n");

    saved = raw;
    reset_events();
    CHECK(crt_diag_format(&raw, buf, sizeof(buf)) == n && equal(buf, expected));
    CHECK(crt_diag_format(&raw, again, sizeof(again)) == n && equal(again, expected));
    CHECK(ev_n == 0 && mem_n == 0);
    for (unsigned int i = 0; i < sizeof(raw); i++) CHECK(((u8 *)&raw)[i] == ((u8 *)&saved)[i]);
    for (int cap = 0; cap <= n + 2; cap++) {
        for (unsigned int i = 0; i < sizeof(again); i++) again[i] = '!';
        int rc = crt_diag_format(&raw, again + 1, cap);
        CHECK(rc == (cap > n ? n : -1));
        CHECK(again[0] == '!');
        for (unsigned int i = cap + 1; i < sizeof(again); i++) CHECK(again[i] == '!');
        if (cap > 0) {
            int kept = cap > n ? n : cap - 1;
            CHECK(again[kept + 1] == 0);
            for (int i = 0; i < kept; i++) CHECK(again[i + 1] == expected[i]);
        }
    }
    CHECK(crt_diag_format(&raw, 0, 0) == -1);
    CHECK(crt_diag_format(&raw, again, -1) == -1);
    int lines = 1, col = 0;
    for (int i = 0; i <= n; i++) {
        if (buf[i] == '\n' || !buf[i]) {
            CHECK(col <= 220); col = 0;
            if (buf[i]) lines++;
        } else col++;
    }
    CHECK(lines == 2);
    output("PASS format: exact keys/counts, every capacity, sentinels, purity\n");

    reset_events();
    crt_diag_bootlog();
    check_events();
    CHECK(print_n == 1 && bootlog_len() == (u32)n + 1 && bootlog_dropped() == 0);
    u32 total;
    const char *log = bootlog_compose("# test\n", &total);
    CHECK(total > (u32)n + 8);
    for (int i = 0; i < n; i++) CHECK(log[i + 7] == expected[i]);
    CHECK(log[n + 7] == '\n');
    output("PASS bootlog: one kprintf, both complete lines retained\n");
    output(expected); output("\n");

    all_ff = 1;
    reset_events();
    crt_diag_read(&raw);
    check_events();
    CHECK(raw.hsync == 0xFF && raw.sysa == 0xFF);
    for (int i = 0; i < 12; i++) CHECK(raw.sdip[i] == 0xFF);
    finish(0);
}
