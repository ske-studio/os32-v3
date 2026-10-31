/* Real driver code with port I/O, PIO, IRQ flags and time supplied by the host.
 * The assembly PIO implementation and physical hardware are not exercised. */
#define IO_H
#include "types.h"
static unsigned int inp(unsigned int port);
static void outp(unsigned int port, unsigned int value);
#include "cpu.h"
/* Linux keeps the real IF bit set. Mask its observation at the CPU boundary
 * so irq_save/restore and IF=0 foreground waits can be exercised unchanged. */
static unsigned int host_if_mask = EFLAGS_IF;
#undef EFLAGS_IF
#define EFLAGS_IF host_if_mask
static unsigned int irq_save(void)
{
    unsigned int saved = host_if_mask;
    host_if_mask = 0;
    return saved;
}
static void irq_restore(unsigned int flags) { host_if_mask = flags; }
#ifndef NE2000_SOURCE
#define NE2000_SOURCE "../../drivers/net/ne2000/ne2000.c"
#endif
#include NE2000_SOURCE

#ifdef HOST_NIC_ST
_Static_assert(__builtin_offsetof(struct ne2k_dev, state) == HOST_NIC_STATE, "state offset");
_Static_assert(__builtin_offsetof(struct ne2k_dev, irq_on) == HOST_NIC_IRQ_ON, "IRQ offset");
_Static_assert(__builtin_offsetof(struct ne2k_dev, rxq_count) == HOST_NIC_RXQ_COUNT, "queue offset");
_Static_assert(__builtin_offsetof(struct ne2k_dev, st) == HOST_NIC_ST, "stats offset");
#endif

enum { REG_BASE = 0x10d0, DATA_PORT = 0x10e0, RAM_ADDR = 0x4567,
       GUARD = 0xa5, NEVER = 1001, EVENT_CAP = 8192 };
enum { READ_REG, WRITE_REG, PIO_READ, PIO_WRITE, DELAY };
struct event { unsigned int kind, port, value; };
static struct event events[EVENT_CAP];
static unsigned int event_count, cursor, pio_count, polls, pending;
static unsigned int ready_after[2], expected_len;
static int expected_write;
static u8 buffer[1516];
static int device_mode, reset_fails, dma_fails;
static u8 regs[2][16], command, txp, tx_status, ram[65536];
static unsigned int reset_count, device_pios, tx_starts, overwrite_writes, ovw_delays;
volatile u32 tick_count;

#ifndef TEST_GROUP
#define TEST_GROUP 0
#endif

void *kmemcpy(void *dst, const void *src, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = ((const u8 *)src)[i];
    return dst;
}

void *kmemset(void *dst, int value, u32 n)
{
    for (u32 i = 0; i < n; i++) ((u8 *)dst)[i] = (u8)value;
    return dst;
}

int tick_register(unsigned int slot, tick_fn fn)
{
    return slot == PC98_TICK_NE2K && fn == ne2k_timer_tick ? 0 : -1;
}

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

static unsigned int device_read(unsigned int port)
{
    if (port == REG_BASE + 0x1f) {
        reset_count++;
        regs[0][NE2K_P0_ISR] = reset_fails ? 0 : NE2K_ISR_RST;
        txp = 0;
        return 0;
    }
    CHECK(port >= REG_BASE && port < REG_BASE + 16);
    unsigned int reg = port - REG_BASE;
    if (reg == NE2K_CR) return command | txp;
    unsigned int page = command & NE2K_CR_PAGE_MASK;
    if (page == NE2K_CR_PAGE1) return regs[1][reg];
    if (page == NE2K_CR_PAGE2) return regs[0][reg];
    if (reg == NE2K_P0_TSR) return tx_status;
    if (reg >= NE2K_P0_CNTR0) return 0;
    return regs[0][reg];
}

static unsigned int inp(unsigned int port)
{
    if (device_mode) return device_read(port);
    CHECK(port == REG_BASE + NE2K_P0_ISR && pio_count > 0 && pio_count <= 2);
    unsigned int value = pending && polls >= ready_after[pio_count - 1]
                       ? NE2K_ISR_RDC : 0;
    polls++;
    record(READ_REG, port, value);
    return value;
}

static void outp(unsigned int port, unsigned int value)
{
    if (device_mode) {
        CHECK(port >= REG_BASE && port < REG_BASE + 16);
        unsigned int reg = port - REG_BASE;
        if (reg == NE2K_CR) {
            command = (u8)(value & ~NE2K_CR_TXP);
            if (value & NE2K_CR_STP) txp = 0;
            if (value & NE2K_CR_TXP) { txp = NE2K_CR_TXP; tx_starts++; }
        } else if ((command & NE2K_CR_PAGE_MASK) == NE2K_CR_PAGE1) {
            regs[1][reg] = (u8)value;
        } else if (reg == NE2K_P0_ISR) {
            regs[0][reg] &= (u8)~value;
        } else {
            regs[0][reg] = (u8)value;
        }
        return;
    }
    record(WRITE_REG, port, value);
    if (port == REG_BASE + NE2K_P0_ISR && (value & NE2K_ISR_RDC)) pending = 0;
}

void cpu_delay_us(u32 us)
{
    if (device_mode) {
        if (us == NE2K_OVW_WAIT_US) ovw_delays++;
        return;
    }
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

static void device_pio(unsigned int port, void *buf, unsigned int len, int write)
{
    CHECK(port == DATA_PORT);
    CHECK((command & NE2K_CR_RD_MASK) == (write ? NE2K_CR_RD1 : NE2K_CR_RD0));
    unsigned int addr = regs[0][NE2K_P0_RSAR0] | (regs[0][NE2K_P0_RSAR1] << 8);
    unsigned int count = regs[0][NE2K_P0_RBCR0] | (regs[0][NE2K_P0_RBCR1] << 8);
    CHECK(count == len + (len & 1) && addr + count <= sizeof(ram));
    device_pios++;
    if (write && txp && addr == ((unsigned int)nic.tx_page << 8)) overwrite_writes++;
    if (write) {
        kmemcpy(ram + addr, buf, len);
        if (len & 1) ram[addr + len] = 0; /* PIO asm's odd final word. */
    } else {
        kmemcpy(buf, ram + addr, len);
    }
    if (!dma_fails) regs[0][NE2K_P0_ISR] |= NE2K_ISR_RDC;
}

void ne2k_pio_read(unsigned int port, void *buf, unsigned int len)
{
    if (device_mode) { device_pio(port, buf, len, 0); return; }
    pio(port, buf, len, 0);
    for (unsigned int i = 0; i < len; i++) ((u8 *)buf)[i] = (u8)(i + pio_count);
}

void ne2k_pio_write(unsigned int port, const void *buf, unsigned int len)
{
    if (device_mode) { device_pio(port, (void *)buf, len, 1); return; }
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
    CHECK(nic.st.rdc_timeout == 7 + failures && nic.st.rdc_retry == 11 + (attempts == 2));
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

static struct ne2k_config device_setup(void)
{
    device_mode = 1;
    host_if_mask = 0x200;
    tick_count = 0;
    reset_fails = dma_fails = 0;
    reset_count = device_pios = tx_starts = overwrite_writes = ovw_delays = 0;
    command = txp = tx_status = 0;
    kmemset(regs, 0, sizeof(regs));
    kmemset(ram, 0, sizeof(ram));
    kmemset(&nic, 0, sizeof(nic));
    /* Valid doubled PROM and 16KiB RAM; the real bring_up probes both. */
    for (unsigned int i = 0; i < NE2K_ETH_ADDR_LEN; i++) {
        ram[2 * i] = ram[2 * i + 1] = (u8)(2 + 2 * i);
    }
    for (unsigned int i = 28; i < 32; i++) ram[i] = NE2K_PROM_SIG;
    return (struct ne2k_config){.reg_base = REG_BASE, .data_port = DATA_PORT,
        .reset_port = REG_BASE + 0x1f, .ram_page_first = 0x40,
        .ram_pages_max = NE2K_RAM_PAGES_16K};
}

static void device_init(void)
{
    struct ne2k_config config = device_setup();
    CHECK(ne2k_init(&config) == NE2K_OK);
    CHECK(nic.state == NE2K_STATE_RUNNING && reset_count == 1);
    CHECK(nic.imr_mask == 0 && nic.imr_written == 0 && regs[0][NE2K_P0_IMR] == 0);
    ne2k_irq_enable();
    CHECK(nic.imr_written == NE2K_IMR_MASK && regs[0][NE2K_P0_IMR] == NE2K_IMR_MASK);
}

static void queue_frames(unsigned int count)
{
    u8 page = regs[1][NE2K_P1_CURR];
    for (unsigned int i = 0; i < count; i++) {
        u8 next = ne2k_ring_advance(nic.rx_start, nic.rx_stop, page, 1);
        u8 *hdr = ram + ((unsigned int)page << 8);
        hdr[0] = 1;
        hdr[1] = next;
        hdr[2] = NE2K_RX_HDR_LEN + NE2K_ETH_MIN_LEN + NE2K_ETH_FCS_LEN;
        hdr[3] = 0;
        kmemset(hdr + NE2K_RX_HDR_LEN, 0x5a, NE2K_ETH_MIN_LEN);
        page = next;
    }
    regs[1][NE2K_P1_CURR] = page;
    regs[0][NE2K_P0_ISR] |= NE2K_ISR_PRX;
}

/* Start with real budget exhaustion, then recover an OVW with an old TX result.
 * A pending PTX/TXE must never release the next, still-transmitting frame. */
static void run_ovw_completion(u8 completion, unsigned int frames)
{
    if (frames > NE2K_OVW_DRAIN_BUDGET) {
        struct ne2k_config config = device_setup();
        config.ram_pages_max = 2 * NE2K_RAM_PAGES_16K;
        CHECK(ne2k_init(&config) == NE2K_OK);
        ne2k_irq_enable();
    } else {
        device_init();
    }
    CHECK(ne2k_send(buffer, 60) == NE2K_OK);
    queue_frames(frames);
    ne2k_poll(4);
    CHECK(nic.rx_backlog && nic.rx_next != regs[1][NE2K_P1_CURR]);
    CHECK(nic.imr_written == 0);
    txp = 0;
    tx_status = completion == NE2K_ISR_PTX ? NE2K_TSR_PTX : NE2K_TSR_ABT;
    regs[0][NE2K_P0_ISR] |= NE2K_ISR_OVW | completion;
    ne2k_timer_tick();
    CHECK(nic.state == NE2K_STATE_OVW_WAIT && nic.tx_busy);
    tick_count++;
    ne2k_timer_tick();
    CHECK(nic.state == NE2K_STATE_OVW_WAIT && tx_starts == 1);
    tick_count++;
    ne2k_timer_tick();
    CHECK(nic.state == NE2K_STATE_RUNNING);
    u8 recovered_backlog = nic.rx_backlog;
    /* Deliberately test A/B first so the pre-fix RED is the TX corruption. */
    CHECK(ne2k_send(buffer, 61) == NE2K_OK);
    unsigned int before = device_pios;
    CHECK(ne2k_send(buffer, 60) == NE2K_ERR_BUSY);
    CHECK(device_pios == before && overwrite_writes == 0 && tx_starts == 2);
    CHECK(recovered_backlog == (frames > 4 + NE2K_OVW_DRAIN_BUDGET));
    CHECK(nic.rx_backlog == (frames > 4 + NE2K_OVW_DRAIN_BUDGET));
    CHECK((nic.rx_next != regs[1][NE2K_P1_CURR]) == (nic.rx_backlog != 0));
    CHECK(nic.imr_written == (nic.rx_backlog ? 0 : NE2K_IMR_MASK));
    CHECK(!(regs[0][NE2K_P0_ISR] & (NE2K_ISR_PTX | NE2K_ISR_TXE)));
    CHECK(nic.st.tx_ok == (completion == NE2K_ISR_PTX));
    CHECK(nic.st.tx_err == (completion == NE2K_ISR_TXE));
}

static void run_reinit_limit(void)
{
    device_init();
    for (unsigned int i = 0; i < 4; i++) {
        CHECK(ne2k_send(buffer, 60) == NE2K_OK);
        tick_count += NE2K_TX_TIMEOUT_TICKS;
        ne2k_timer_tick();
        CHECK(nic.st.tx_timeouts == i + 1);
        CHECK(reset_count == 1 + (i < 3 ? i + 1 : 3));
        CHECK(nic.st.reinit == (i < 3 ? i + 1 : 3));
        CHECK(nic.state == (i < 3 ? NE2K_STATE_RUNNING : NE2K_STATE_FAILED));
    }
    CHECK(nic.imr_written == 0 && regs[0][NE2K_P0_IMR] == 0);
}

static void run_ovw_waits(void)
{
    /* The IF=0 foreground path waits once, then either resumes or fails. */
    for (int fail = 0; fail <= 1; fail++) {
        device_init();
        CHECK(ne2k_send(buffer, 60) == NE2K_OK);
        queue_frames(1);
        regs[0][NE2K_P0_ISR] |= NE2K_ISR_OVW;
        ne2k_poll(4);
        CHECK(nic.state == NE2K_STATE_OVW_WAIT);
        host_if_mask = 0;
        dma_fails = reset_fails = fail;
        ne2k_poll(4);
        CHECK(ovw_delays == 1);
        CHECK(nic.state == (fail ? NE2K_STATE_FAILED : NE2K_STATE_RUNNING));
        CHECK(tx_starts == (fail ? 1 : 2));
        CHECK(nic.imr_written == (fail ? 0 : NE2K_IMR_MASK));
        CHECK(regs[0][NE2K_P0_IMR] == nic.imr_written);
        ne2k_poll(4);
        CHECK(ovw_delays == 1);
    }
}

static void run_send_lengths(void)
{
    static const unsigned int lens[] = {14, 60, 61, 100, 255, 256, 257, 1000, 1513, 1514};
    for (unsigned int k = 0; k < sizeof(lens) / sizeof(lens[0]); k++) {
        device_init();
        unsigned int len = lens[k], padded = len < 60 ? 60 : len;
        for (unsigned int i = 0; i < len; i++) buffer[i] = (u8)(i ^ 0x5a);
        kmemset(nic.tx_buf, GUARD, sizeof(nic.tx_buf));
        kmemset(ram + ((unsigned int)nic.tx_page << 8), GUARD, sizeof(nic.tx_buf));
        CHECK(ne2k_send(buffer, len) == NE2K_OK);
        CHECK(((unsigned int)regs[0][NE2K_P0_TBCR0] | (regs[0][NE2K_P0_TBCR1] << 8)) == padded);
        for (unsigned int i = 0; i < padded + (padded & 1); i++) {
            CHECK(ram[((unsigned int)nic.tx_page << 8) + i] == (i < len ? buffer[i] : 0));
        }
        CHECK(ram[((unsigned int)nic.tx_page << 8) + padded + (padded & 1)] == GUARD);
        CHECK(nic.st.tx_bytes == len && nic.st.tx_accepted == 1);
    }
}

static void run_init_failure(void)
{
    struct ne2k_config config = device_setup();
    /* A re-init must clear a previous IRQ-enabled shadow as well. */
    nic.imr_mask = nic.imr_written = regs[0][NE2K_P0_IMR] = NE2K_IMR_MASK;
    reset_fails = 1;
    CHECK(ne2k_init(&config) == NE2K_ERR_TIMEOUT);
    CHECK(nic.state == NE2K_STATE_FAILED && reset_count == 1);
    CHECK(nic.imr_mask == 0 && nic.imr_written == 0 && regs[0][NE2K_P0_IMR] == 0);
}

void _start(void)
{
    if (TEST_GROUP == 0 || TEST_GROUP == 2) {
        run_ovw_completion(NE2K_ISR_PTX, 5);
        run_ovw_completion(NE2K_ISR_TXE, 5);
        run_ovw_completion(NE2K_ISR_PTX, 70);
        run_ovw_completion(NE2K_ISR_TXE, 70);
    }
    if (TEST_GROUP == 0 || TEST_GROUP == 3) run_reinit_limit();
    if (TEST_GROUP == 0 || TEST_GROUP == 4) {
        run_ovw_waits();
        run_send_lengths();
        run_init_failure();
    }
    device_mode = 0;
    if (TEST_GROUP == 0 || TEST_GROUP == 1) {
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
    }
    finish(0);
}
