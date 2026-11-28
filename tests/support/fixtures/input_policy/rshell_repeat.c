#include "userland/shell/rshell.c"

/* One pending repeat per keyboard. Model arrivals independently of pumps. */
static unsigned reads, due, delivered, pending, ready, notice_pending;

static int uart_char(void)
{
    ++reads;
    if (reads == 800) notice_pending = 1;
    if (reads <= 4096 && reads % 1024 == 0) {
        ++due;
        pending = 1;
    }
    if (reads == 3000) return 'b';
    if (reads == 42000) return '\r';
    return -1;
}

static int pump(unsigned int budget)
{
    if (budget && pending) {
        pending = 0;
        ++ready;
        ++delivered;
    }
    return 0;
}

static int local_char(void)
{
    if (!ready) return -1;
    --ready;
    return 'a';
}

static int read_notice(u32 generation, u32 service, HostNotice *out)
{
    (void)generation;
    (void)service;
    (void)out;
    notice_pending = 0;
    return OS32_ERR_AGAIN;
}

static KernelAPI api = { .io_pump = pump, .kbd_trygetchar_local = local_char,
                         .serial_trygetchar = uart_char, .host_notice_read = read_notice };
KernelAPI *g_api = &api;

int probe(void)
{
    char buf[8];
    struct rsh_line line;
    rsh_uart = 1;
    rsh_line_begin(&line, buf, sizeof(buf));
    int r = rsh_line_feed(&line, 's', 1, 1, 0);
    r = rsh_line_rest(&line, r, -1, 0, NULL, &rsh_io_real);
    const char expected[] = "saabaa";
    if (r != RSH_LINE_DONE || reads != 42000 || !notice_pending || !line.local ||
        due != 4 || delivered != due || line.pos != sizeof(expected) - 1) return 1;
    for (int i = 0; i < line.pos; ++i)
        if (buf[i] != expected[i]) return 2;
    return 0;
}
