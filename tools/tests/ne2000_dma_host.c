/* Real DMA code; only port I/O, PIO and elapsed time are supplied by the host.
 * The assembly PIO implementation and physical hardware are not exercised. */
#define IO_H
#include "types.h"
static unsigned int inp(unsigned int port);
static void outp(unsigned int port, unsigned int value);
static unsigned int irq_save(void) { return 0; }
static void irq_restore(unsigned int flags) { (void)flags; }
#ifndef NE2000_SOURCE
#define NE2000_SOURCE "../../drivers/net/ne2000/ne2000.c"
#endif
#include NE2000_SOURCE

enum { REG_BASE = 0x10d0, DATA_PORT = 0x10e0, RAM_ADDR = 0x4567,
       GUARD = 0xa5, NEVER = 1001, EVENT_CAP = 8192 };
enum { READ_REG, WRITE_REG, PIO_READ, PIO_WRITE, DELAY };
struct event { unsigned int kind, port, value; };
static struct event events[EVENT_CAP];
static unsigned int event_count, cursor, pio_count, polls, pending;
static unsigned int ready_after[2], expected_len;
static int expected_write;
static u8 buffer[1516];

static void finish(unsigned int rc)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(rc));
    for (;;) {}
}
#define CHECK(x) do { if (!(x)) finish(__LINE__ % 200 + 1); } while (0)

static void record(unsigned int kind, unsigned int port, unsigned int value)
{
    CHECK(event_count < EVENT_CAP);
    events[event_count++] = (struct event){kind, port, value};
}

static unsigned int inp(unsigned int port)
{
    CHECK(port == REG_BASE + NE2K_P0_ISR && pio_count > 0 && pio_count <= 2);
    unsigned int value = pending && polls >= ready_after[pio_count - 1]
                       ? NE2K_ISR_RDC : 0;
    polls++;
    record(READ_REG, port, value);
    return value;
}

static void outp(unsigned int port, unsigned int value)
{
    record(WRITE_REG, port, value);
    if (port == REG_BASE + NE2K_P0_ISR && (value & NE2K_ISR_RDC)) pending = 0;
}

void cpu_delay_us(u32 us)
{
    record(DELAY, 0, us);
}

static void pio(unsigned int port, const void *buf, unsigned int len, int write)
{
    CHECK(port == DATA_PORT && buf == buffer + 1 && len == expected_len);
    CHECK(write == expected_write && pio_count < 2);
    record(write ? PIO_WRITE : PIO_READ, port, len);
    pio_count++;
    polls = 0;
    pending = 1;
}

void ne2k_pio_read(unsigned int port, void *buf, unsigned int len)
{
    pio(port, buf, len, 0);
    for (unsigned int i = 0; i < len; i++) ((u8 *)buf)[i] = (u8)(i + pio_count);
}

void ne2k_pio_write(unsigned int port, const void *buf, unsigned int len)
{
    pio(port, buf, len, 1);
    for (unsigned int i = 0; i < len; i++) CHECK(((const u8 *)buf)[i] == (u8)(i ^ 0x5a));
}

static void expect(unsigned int kind, unsigned int port, unsigned int value)
{
    CHECK(cursor < event_count);
    struct event e = events[cursor++];
    CHECK(e.kind == kind && e.port == port && e.value == value);
}

static void run_case(int write, unsigned int len, int running,
                     unsigned int first, unsigned int second)
{
    event_count = cursor = pio_count = polls = 0;
    pending = 1; /* A stale RDC must be acknowledged before issuing DMA. */
    ready_after[0] = first;
    ready_after[1] = second;
    expected_write = write;
    expected_len = len;
    nic.cfg.reg_base = REG_BASE;
    nic.cfg.data_port = DATA_PORT;
    nic.state = running ? NE2K_STATE_RUNNING : NE2K_STATE_STOPPED;
    nic.st.rdc_timeout = 7;
    nic.st.rdc_retry = 11;
    for (unsigned int i = 0; i < sizeof(buffer); i++) buffer[i] = GUARD;
    if (write) {
        for (unsigned int i = 0; i < len; i++) buffer[i + 1] = (u8)(i ^ 0x5a);
    }

    int rc = write ? dma_write(RAM_ADDR, buffer + 1, (u16)len)
                   : dma_read(RAM_ADDR, buffer + 1, (u16)len);
    unsigned int attempts = len ? (first == NEVER ? 2 : 1) : 0;
    unsigned int failures = len ? (first == NEVER) + (first == NEVER && second == NEVER) : 0;
    CHECK(rc == (failures == 2 ? NE2K_ERR_TIMEOUT : NE2K_OK));
    CHECK(pio_count == attempts);
    CHECK(nic.st.rdc_timeout == 7 + failures && nic.st.rdc_retry == 11 + failures);
    CHECK(nic.state == (running ? NE2K_STATE_RUNNING : NE2K_STATE_STOPPED));
    for (unsigned int i = 0; i < sizeof(buffer); i++) {
        u8 value = GUARD;
        if (i > 0 && i <= len) value = write ? (u8)((i - 1) ^ 0x5a) : (u8)(i - 1 + attempts);
        CHECK(buffer[i] == value);
    }

    for (unsigned int a = 0; a < attempts; a++) {
        unsigned int count = len + (len & 1);
        expect(WRITE_REG, REG_BASE + NE2K_P0_ISR, NE2K_ISR_RDC);
        expect(WRITE_REG, REG_BASE + NE2K_P0_RBCR0, count & 0xff);
        expect(WRITE_REG, REG_BASE + NE2K_P0_RBCR1, count >> 8);
        expect(WRITE_REG, REG_BASE + NE2K_P0_RSAR0, RAM_ADDR & 0xff);
        expect(WRITE_REG, REG_BASE + NE2K_P0_RSAR1, RAM_ADDR >> 8);
        expect(WRITE_REG, REG_BASE + NE2K_CR,
               NE2K_CR_STA | (write ? NE2K_CR_RD1 : NE2K_CR_RD0));
        expect(write ? PIO_WRITE : PIO_READ, DATA_PORT, len);
        unsigned int delays = ready_after[a] == NEVER ? 1000 : ready_after[a];
        for (unsigned int i = 0; i < delays; i++) {
            expect(READ_REG, REG_BASE + NE2K_P0_ISR, 0);
            expect(DELAY, 0, 4);
        }
        if (ready_after[a] == NEVER) {
            expect(READ_REG, REG_BASE + NE2K_P0_ISR, 0);
            expect(WRITE_REG, REG_BASE + NE2K_CR,
                   NE2K_CR_RD2 | (running ? NE2K_CR_STA : NE2K_CR_STP));
        } else {
            expect(READ_REG, REG_BASE + NE2K_P0_ISR, NE2K_ISR_RDC);
            expect(WRITE_REG, REG_BASE + NE2K_P0_ISR, NE2K_ISR_RDC);
        }
    }
    CHECK(cursor == event_count);
}

void _start(void)
{
    static const unsigned int lengths[] = {0, 1, 2, 255, 256, 257, 1514};
    for (int write = 0; write <= 1; write++) {
        for (int running = 0; running <= 1; running++) {
            for (unsigned int i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
                run_case(write, lengths[i], running, 0, 0);
                run_case(write, lengths[i], running, 3, 0);
                run_case(write, lengths[i], running, 1000, 0); /* Exact deadline success. */
                run_case(write, lengths[i], running, NEVER, 3);
                run_case(write, lengths[i], running, NEVER, NEVER);
            }
        }
    }
    finish(0);
}
