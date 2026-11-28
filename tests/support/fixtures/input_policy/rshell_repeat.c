#include "userland/shell/rshell.c"

static int ready, delivered, pumps;

static int pump(unsigned int budget)
{
    if (budget && !delivered) ready = 1;
    ++pumps;
    return 0;
}

static int local_char(void)
{
    if (!ready || delivered) return -1;
    delivered = 1;
    return 'a';
}

static KernelAPI api = { .io_pump = pump, .kbd_trygetchar_local = local_char };
KernelAPI *g_api = &api;

int probe(void)
{
    char buf[8];
    struct rsh_line line;
    rsh_line_begin(&line, buf, sizeof(buf));
    int r = rsh_line_feed(&line, 'a', 0, 1, 0);
    r = rsh_line_rest(&line, r, -1, 0, NULL, &rsh_io_real);
    return !(r == RSH_LINE_DONE && line.pos == 2 && buf[1] == 'a' &&
             line.local && pumps < RSH_REST_SPIN);
}
