/* Foreground-only LAN input for the resident CLI. */
#include "lan_boot.h"
#include "kernel/cli/resident_cli.h"

enum { INPUT_CAPACITY = 1024 };
static u8 ring[INPUT_CAPACITY];
static u32 head, count;
static int cancel_pending, discard;

void pm_terminal_line_lost(void)
{
    head = count = 0;
    cancel_pending = 1;
    discard = 1;
}

void pm_terminal_open(void)
{
    pm_terminal_line_lost();
    discard = 0;
}

void pm_terminal_feed(const u8 *bytes, u32 len)
{
    for (u32 i = 0; i < len; ++i) {
        u8 c = bytes[i];
        if (discard) {
            if (c == '\n') discard = 0;
            continue;
        }
        if (count == INPUT_CAPACITY) {
            pm_terminal_line_lost();
            if (c == '\n') discard = 0;
            continue;
        }
        ring[(head + count) % INPUT_CAPACITY] = c;
        ++count;
    }
}

int pm_terminal_trygetkey(void)
{
    int c;
    if (cancel_pending) {
        cancel_pending = 0;
        return RESIDENT_CLI_KEY_CANCEL;
    }
    if (!count) return -1;
    c = ring[head];
    head = (head + 1) % INPUT_CAPACITY;
    --count;
    return c;
}

#ifdef PM_TERMINAL_TEST
extern u64 pm_terminal_tb(void);
#else
static u64 pm_terminal_tb(void)
{
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((u64)hi << 32) | lo;
}
#endif

void pm_terminal_poll(void)
{
    pm_lan_poll(pm_terminal_tb());
}

int pm_terminal_getkey(void)
{
    int c;
    while ((c = pm_terminal_trygetkey()) < 0) pm_terminal_poll();
    return c;
}
