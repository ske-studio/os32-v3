#include "in2_int.h"
static u32 now, command_start;
static int capture, frozen, pending, serial_reads, steps, ready_at, value;
static u32 ticks(void) { return now; }
static u32 timeout(void) { return 3000; }
static int cap(void) { return capture; }
static int stop(void) { return pending; }
static int local(void) { return now >= (u32)ready_at ? value : -1; }
static int serial(void) { serial_reads++; return -1; }
static int enabled(void) { return !capture; }
static void step(void) { now += 100; steps++; if (steps > 100) __builtin_trap(); }
static const struct InputLegacyProvider provider = {
    .try_key=local, .terminal_try=serial, .terminal_enabled=enabled,
    .wait_ticks=ticks, .wait_timeout_ticks=timeout, .capture_active=cap,
    .wait_pending=stop, .wait_step=step,
};
/* Compile the current x86 implementations against the same fake devices. */
#define tick_count now
#define kbd_count (local() >= 0)
#define KBD_RSHELL_TIMEOUT_SEC 30
#define PIT_HZ 100
#define KBD_BUF_SIZE 1
#define RING_DEQUEUE(entry, buf, head, count, size) do { (entry)=value; } while (0)
static int kbd_gui_mode, rshell_active;
static int con_sink_capture_active(void) { return capture; }
static void kbd_foreground_poll(void) {}
static int serial_trygetchar(void) { return serial(); }
static int ring3_wait_pending(void) { return pending; }
static void idle_work_run(void) {}
static void _halt(void) { step(); }
static int kbd_gui_getbyte(int interruptible) { (void)interruptible; return -1; }
#include "old_wait.inc"
static int old_wait(int mode, int interruptible) {
    return mode == IL_KEY ? kbd_getkey_wait(interruptible) : kbd_getchar_wait(interruptible);
}
static int run(int old, int mode, int interruptible) {
    return old ? old_wait(mode,interruptible) : input_legacy_wait(mode,interruptible);
}
static int probe(int old) {
    input_legacy_register(&provider);
    capture=1; frozen=0; pending=0; serial_reads=0; ready_at=100000; steps=0;
    command_start=100; now=command_start+1700;
    if (run(old,IL_CHAR,1)!=13 || now!=4800 || !capture) return 1;
    steps=0;
    if (run(old,IL_KEY,1)!=27 || now!=7800 || serial_reads || !capture) return 2;
    frozen=1; steps=0;
    if (run(old,IL_CHAR,1)!=13 || !capture || !frozen || now!=10800) return 3;
    pending=1; steps=0;
    if (run(old,IL_KEY,1)!=-1 || steps) return 4;
    pending=0; capture=0; ready_at=11000; value=0x1d61; steps=0;
    if (run(old,IL_KEY,1)!=0x1d61 || serial_reads) return 5;
    return 0;
}
#ifndef PPC_COMPILE
void _start(void) { int rc=probe(1); if (!rc) rc=probe(0); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); __builtin_unreachable(); }
#else
int fixture(void) { return probe(0); }
#endif
