/* b27213d2 keyboard selection and the new bridge use identical source queues. */
#include "types.h"
#include "in2_int.h"
static int kbd_gui_mode, rshell_active, capture, serial, injected;
static u16 kbd_buf[2];
static unsigned kbd_head, kbd_count, parks, polls, serial_reads, halts;
static volatile u32 tick_count = 123;
#define KBD_BUF_SIZE 2
#define RING_DEQUEUE(e, b, h, n, cap) do { e=b[h]; h=(h+1)%(cap); n--; } while (0)
static unsigned irq_save(void) { return 0; }
static void irq_restore(unsigned f) { (void)f; }
static int con_sink_capture_active(void) { return capture; }
static void kbd_foreground_poll(void) { if (!kbd_gui_mode) polls++; }
static int serial_peekchar(void) { serial_reads++; return serial; }
static int serial_trygetchar(void) { serial_reads++; int v=serial; serial=-1; return v; }
static int kbd_inject_peek(u8 *b) { if (injected<0) return 0; *b=(u8)injected; return 1; }
static int kbd_inject_take(u8 *b) { int ok=kbd_inject_peek(b); if (ok) injected=-1; return ok; }
static int exec_park_poll(u32 tick) { if (tick != tick_count) __builtin_trap(); parks++; return 0; }
#define KBD_RSHELL_TIMEOUT_SEC 30
#define PIT_HZ 100
static int exec_park_kbd(void) { parks++; return 0; }
static int ring3_wait_pending(void) { return 0; }
static void idle_work_run(void) {}
/* Finish an empty wait with local input; unexpected serial reads stay visible. */
static void _halt(void) {
    if (++halts > 1) __builtin_trap();
    kbd_buf[0]=0x1d61; kbd_head=0; kbd_count=1; injected=0xe3;
}
#include "selected.inc"
#ifdef CURRENT
static int samples, completed = -1;
static int finite_peek(void) {
    if (samples++ == 0) completed=0x1d61;
    return completed;
}
static int completed_take(void) { int key=completed; completed=-1; return key; }
static int finite_peek_contract(void) {
    static const struct InputLegacyProvider finite = {
        .peek_key=finite_peek, .try_key=completed_take,
    };
    input_legacy_register(&finite);
    /* A provider may sample finitely while peeking, then retain the full key. */
    if (input_legacy_peek(IL_KEY)!=0x1d61 || samples!=1) return 3;
    if (input_legacy_peek(IL_CHAR)!='a' || completed!=0x1d61) return 4;
    if (input_legacy_try(IL_LOCAL_CHAR)!='a' || completed!=-1) return 5;
    return 0;
}
#endif
static int read_port(int port)
{
    switch (port) {
    case 0: return kbd_trygetchar();
    case 1: return kbd_trygetkey();
    case 2: return kbd_trygetchar_local();
    default: return kbd_peekkey();
    }
}
static int probe(void)
{
#ifdef CURRENT
    input_legacy_register(0);
    if (input_legacy_try(IL_KEY)!=-1 || input_legacy_peek(IL_CHAR)!=-1) return 1;
    static const struct InputLegacyProvider absent = {0};
    input_legacy_register(&absent);
    if (input_legacy_try(IL_KEY)!=-1 || input_legacy_peek(IL_CHAR)!=-1) return 2;
    int rc=finite_peek_contract();
    if (rc) return rc;
    input_legacy_register(&kbd_legacy_provider);
    kbd_gui_mode=1; injected=0xe3; parks=serial_reads=0;
    if (input_legacy_peek(IL_LOCAL_CHAR)!=-1 || parks || serial_reads ||
        injected!=0xe3) return 7;
#endif
    for (int gui=0; gui<2; gui++) for (int rs=0; rs<2; rs++)
    for (int cap=0; cap<2; cap++) for (int ser=0; ser<2; ser++)
    for (int inj=0; inj<2; inj++) for (int local=0; local<2; local++)
    for (int port=0; port<4; port++) {
        kbd_gui_mode=gui; rshell_active=rs; capture=cap;
        serial=ser ? 's' : -1; injected=inj ? 0xe3 : -1;
        kbd_head=0; kbd_count=local; kbd_buf[0]=0x1d61; parks=polls=serial_reads=0;
        int first=read_port(port), second=read_port(port);
        /* Binary trace captures both results, queue state and park/poll counts. */
        int trace[]={first,second,serial,injected,(int)kbd_count,(int)kbd_head,(int)parks,(int)polls,(int)serial_reads};
        __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(trace),"d"(sizeof(trace)) : "memory");
    }
    for (int gui=0; gui<2; gui++) for (int cap=0; cap<2; cap++)
    for (int ready=0; ready<2; ready++) for (int intr=0; intr<2; intr++) {
        kbd_gui_mode=gui; capture=cap; rshell_active=1;
        serial='s'; injected=ready ? 0xe3 : -1;
        kbd_head=0; kbd_count=ready; kbd_buf[0]=0x1d61;
        parks=polls=serial_reads=halts=0;
        int key=kbd_getkey_wait(intr);
        /* Serial remains untouched both with/without capture, even after a halt. */
        if (key!=(gui ? 0xe3 : 0x1d61) || serial_reads || serial!='s') return 6;
        int trace[]={key,serial,(int)halts,(int)parks,(int)polls};
        __asm__ volatile("int $0x80" : : "a"(4),"b"(1),"c"(trace),"d"(sizeof(trace)) : "memory");
    }
    return 0;
}
void _start(void)
{
    int rc=probe();
    __asm__ volatile("int $0x80" : : "a"(1),"b"(rc) : "memory");
    __builtin_unreachable();
}
