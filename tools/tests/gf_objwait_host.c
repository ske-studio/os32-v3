/* Real F1/F2/F3 service composition. Only hardware/OS adapters are fake.
 * Exercise gf3_init via gf3_service_start, with actual FIFO/push/fence code. */
#include "gf_command_runtime_host.h"
static char wait_log[4096];
static unsigned wait_log_len;
static int wait_gate = 1;
int serial_gate_active(void) { return wait_gate; }
void serial_puts_polled(const char *s)
{
    while (*s) {
        assert(wait_log_len + 1 < sizeof(wait_log));
        wait_log[wait_log_len++] = *s++;
    }
    wait_log[wait_log_len] = 0;
}
#define GF_OBJWAIT_SERIAL
#define GF3_CINT_C2
#include "gf_accel_c1_host.c"

static unsigned inject;
static u32 request_dl;
static bool resumed;
static void observe_wait(struct gf3_host *h, const struct gf3_host_event *e)
{
    cint_observe(h, e);
    /* Polling runs for two seconds; retain counters, recycle event storage. */
    if (h->nevents > CINT_EVENTS / 2) h->nevents = 0;
    if (inject == 1 && e->op == H_READ && e->a[1] == 0x3250)
        *(u32 *)e->p[1] |= 0x1000; /* stuck HASH_BUSY, even after bit0 clear */
    if (e->op == H_WRITE && e->a[1] == 0x2500 && e->a[3] == 1)
        resumed = true;
    if (inject == 2 && resumed && e->op == H_CLOCK)
        *(u32 *)e->p[1] = h->now = request_dl; /* first setup reserve */
}
static void reset_wait(void)
{
    wait_gate = 1;
    cint_reset();
    request_dl = cint_deadline();
    cint_host.observe = observe_wait;
    inject = 0; resumed = false;
    wait_log_len = 0; wait_log[0] = 0; wait_gate = 0;
}
static void expect_number(const char *field, u32 value, bool hex)
{
    char saved[sizeof(wait_log)];
    unsigned saved_len = wait_log_len;
    gf2_host_memcpy(saved, wait_log, sizeof(saved));
    wait_log_len = 0; serial_puts_polled(field);
    if (hex) serial_diag_hex(value, 8);
    else serial_diag_u32(value);
    assert(gf2_host_strstr(saved, wait_log));
    gf2_host_memcpy(wait_log, saved, sizeof(saved)); wait_log_len = saved_len;
}
static void expect_line(const char *site, const char *fields)
{
    assert(gf2_host_strstr(wait_log, site));
    assert(gf2_host_strstr(wait_log, fields));
    assert(gf2_host_strstr(wait_log, " dl="));
    expect_number(" dl=", request_dl, false);
    expect_number(" now=", request_dl, false);
    assert(gf2_host_strstr(wait_log, " read_rc=0"));
    const char *first = gf2_host_strstr(wait_log, "[gf3] wait timeout site=");
    assert(first && !gf2_host_strstr(first + 1, "[gf3] wait timeout site="));
    int written;
    __asm__ volatile("int $0x80" : "=a"(written)
        : "0"(4), "b"(1), "c"(first), "d"(gf2_host_strlen(first)) : "memory", "cc");
    (void)written;
}
static void cleanup_wait(void)
{
    inject = 0; cint_stall_reference = 0; wait_gate = 1;
    cint_host.observe = cint_observe;
    cint_stop_display();
}
void _start(void)
{
    reset_wait();
    assert(!gf3_service_start(cint_service, request_dl));
    assert(!wait_log_len); /* success emits no diagnostic */
    cleanup_wait();

    reset_wait(); inject = 1;
    assert(gf3_service_start(cint_service, request_dl) == GF2_ETIME);
    expect_line("site=fifo_hash_busy reg=00003250 last=00001000",
                "want=HASH_BUSY&00001000==0");
    cleanup_wait();

    for (unsigned phase = 1; phase <= 2; ++phase) {
        reset_wait(); cint_stall_reference = cint_references + phase;
        u32 seq = cint_prod.chan.seq + phase;
        assert(gf3_service_start(cint_service, request_dl) == GF2_ETIME);
        expect_line("site=fence_reference reg=00c00048", " reference=");
        /* Verify the actual requested sequence and sampled reference. */
        expect_number(" seq=", seq, false);
        expect_number(" reference=", seq - 1, true);
        cleanup_wait();
    }
    reset_wait(); request_dl = cint_host.now; /* first drain reserve */
    u32 put = cint_prod.chan.put, free = cint_prod.chan.free;
    u32 get = cint_prod.chan.B + put * sizeof(u32);
    assert(gf3_service_start(cint_service, request_dl) == GF2_ETIME);
    expect_line("site=push_reserve reg=00c00044", " get=");
    expect_number(" put=", put, false); expect_number(" free=", free, false);
    expect_number(" get=", get, true);
    cleanup_wait();

    reset_wait(); inject = 2; /* setup packet after resume */
    assert(gf3_service_start(cint_service, request_dl) == GF2_ETIME);
    expect_line("site=push_reserve reg=00c00044", " get=");
    cleanup_wait();
    reset_wait(); request_dl = cint_host.now; wait_gate = 1;
    assert(gf3_service_start(cint_service, request_dl) == GF2_ETIME);
    assert(!wait_log_len); /* serial protocol owns the wire */
    cleanup_wait();
    gf2_host_exit(0);
}
