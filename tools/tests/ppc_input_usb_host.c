/* Freestanding ILP32: privileged IRQ boundary only is substituted. */
#include "input_usb.h"
#include "input_boot.h"
#include "input_service.h"
static const InputOps *ops;
static int lan[256], ln, lp, polls, locked;
static u32 now;
void input_register(const InputOps *p) { ops = p; }
unsigned int irq_save(void) { unsigned f = locked; locked = 1; return f; }
void irq_restore(unsigned int f) { locked = f; }
void pm_terminal_poll(void) { if (locked) __builtin_trap(); polls++; }
int pm_terminal_trygetkey(void) { return lp < ln ? lan[lp++] : -1; }
int platform_clock_read_us(u32 *lo, u32 *hi) { *lo = now; *hi = 0; return 0; }
static void write_text(const char *s)
{
    unsigned n = 0; while (s[n]) n++;
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(s), "d"(n) : "memory");
}
static void finish(int code) __attribute__((noreturn));
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code));
    __builtin_unreachable();
}
static void check(int ok, int code) { if (!ok) { write_text("FAIL\n"); finish(code); } }
static void tap(u8 src, u8 sc) { pm_input_usb_key(src, sc, 1); pm_input_usb_key(src, sc, 0); }
static void empty(void) { check(pm_input_usb_take() == -1, 11); }
static void test(void)
{
    write_text("START\n");
    check(pm_input_keymap(0, 0) == 27 && pm_input_keymap(128, 0) == -1, 10);
    static const u8 scans[] = {0x1d,0x2d,0x2b,0x1f,0x12,0x20,0x21,0x22,0x17,0x23,0x24,0x25,0x2f,0x2e,0x18,0x19,0x10,0x13,0x1e,0x14,0x16,0x2c,0x11,0x2a,0x15,0x29};
    for (unsigned i=0; i<26; i++) {
        check(pm_input_keymap(scans[i],0)==(int)('a'+i),10);
        check(pm_input_keymap(scans[i],PM_INPUT_SHIFT)==(int)('A'+i),10);
        check(pm_input_keymap(scans[i],PM_INPUT_CAPS)==(int)('A'+i),10);
        check(pm_input_keymap(scans[i],PM_INPUT_CAPS|PM_INPUT_SHIFT)==(int)('a'+i),10);
        check(pm_input_keymap(scans[i],PM_INPUT_CTRL)==(i==2?27:(int)i+1),10);
    }
    check(pm_input_keymap(1,1)=='!' && pm_input_keymap(2,1)=='"' && pm_input_keymap(10,1)==-1,10);
    check(pm_input_keymap(1,2)=='1' && pm_input_keymap(0x33,1)=='_',10);
    check(pm_input_keymap(0x0e,0)==8 && pm_input_keymap(0x1c,0)==13,10);
    for (unsigned sc=0x39; sc<=0x3e; sc++) check(pm_input_keymap(sc,0)==(int)(sc<<8),10);
    static const u8 punct[] = {1,2,3,4,5,6,7,8,9,10,11,12,13,0x1a,0x1b,0x26,0x27,0x28,0x30,0x31,0x32,0x33};
    static const char base[] = "1234567890-^\\@[;:],./\\";
    static const char upper[] = "!\"#$%&'()\0=~|`{+*}<>?_";
    for(unsigned i=0;i<sizeof(punct);i++) {
        check(pm_input_keymap(punct[i],0)==base[i],10);
        check(pm_input_keymap(punct[i],PM_INPUT_SHIFT)==(upper[i]?upper[i]:-1),10);
    }
    static const char pad[] = "-/789*456+123=0,.";
    for(unsigned i=0;i<17;i++) check(pm_input_keymap(0x40+i,PM_INPUT_SHIFT)==pad[i],10);
    write_text("MAP\n");
    pm_input_usb_key(1,0x70,1); tap(1,0x1d);
    check(pm_input_usb_has_key() && pm_input_usb_has_key(),12);
    check(pm_input_usb_take()=='A',12);
    pm_input_usb_key(2,0x70,1); pm_input_usb_release(1); tap(3,0x1d);
    check(pm_input_usb_take()=='A',12);
    pm_input_usb_release(2); tap(1,0x1d); check(pm_input_usb_take()=='a',12);
    pm_input_usb_key(1,0x71,1); pm_input_usb_key(1,0x71,1); pm_input_usb_repeat(1,0x71);
    tap(1,0x1d); check(pm_input_usb_take()=='A',12);
    pm_input_usb_release(1); tap(2,0x1d); check(pm_input_usb_take()=='A',12);
    tap(1,0x71); tap(1,0x1d); check(pm_input_usb_take()=='a',12);
    pm_input_usb_key(1,0x1d,1); pm_input_usb_key(1,0x1d,1);
    pm_input_usb_repeat(1,0x1d); pm_input_usb_key(1,0x1d,0); pm_input_usb_repeat(1,0x1d);
    check(pm_input_usb_take()=='a' && pm_input_usb_take()=='a',13); empty();
    pm_input_usb_key(1,0x74,1); tap(2,0x2b); check(pm_input_usb_take()==27,13);
    pm_input_usb_release(1);
    tap(1,0x72); tap(1,0x62); empty();
    write_text("STATE\n");
    pm_input_usb_key(1,0x70,1);
    for (unsigned i=0;i<128;i++) tap(2, i&1 ? 0x2d : 0x1d);
    tap(2,0x1d); check(pm_input_usb_dropped()==1,14);
    pm_input_usb_key(1,0x70,0); pm_input_usb_key(3,0x70,1); pm_input_usb_release(3);
    for (unsigned i=0;i<128;i++) check(pm_input_usb_take()==(i&1?'B':'A'),14);
    tap(1,0x1d); check(pm_input_usb_take()=='a',14); empty();
    locked=1; check(!pm_input_usb_has_key() && locked,14); pm_input_usb_take(); check(locked,14); locked=0;
    write_text("FIFO\n");
    tap(1,0x1d); pm_input_boot(); /* Registration retains queued input. */
    lan[0]='x'; lan[1]='y'; ln=2;
    int before=polls;
    check(ops->peekkey()=='a' && ops->peekkey()=='a' && polls==before+1,15);
    check(ops->trygetkey()=='a',15);
    tap(1,0x2d); check(ops->trygetchar()=='x' && ops->trygetkey()=='b' && ops->getkey()=='y',15);
    tap(1,0x3a); check(ops->getchar()==0,15);
    check(ops->dropped_count()==1 && !ops->modifiers() && !ops->shift_state() && !ops->is_pressed(0x70),15);
    ln=lp=0; lan[ln++]=27; check(ops->trygetkey()==-1,15);
    tap(1,0x1d); check(ops->trygetkey()=='a',15);
    now=50000; check(ops->getkey()==27,15);
    ln=lp=0;
    for(unsigned i=0;i<64;i++) lan[ln++]=i&1?'\n':'\r';
    tap(1,0x1d); check(ops->getkey()=='a',15); check(ops->getkey()==13,15);
    tap(1,0x2d); check(ops->getkey()=='b',15);
    write_text("PROVIDER\n");
}
void _start(void) { test(); finish(0); }
