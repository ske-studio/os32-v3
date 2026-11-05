#include "x87.h"

static X87State parent[3], parked;
static unsigned int value[2];
static void control(unsigned short c) { __asm__ volatile("fldcw %0" : : "m"(c)); }
static unsigned short get_control(void) { unsigned short c; __asm__ volatile("fnstcw %0" : "=m"(c)); return c; }
static void push_one(void) { __asm__ volatile("fld1"); }
static int pop_one(void) {
    __asm__ volatile("fstpl %0" : "=m"(value));
    return value[0] == 0 && value[1] == 0x3ff00000;
}
static int run(void) {
    x87_probe();
    x87_init(); control(0x077f); push_one();
    x87_save(&parent[0]); x87_init();
    control(0x0b7f); push_one();
    x87_save(&parent[1]); x87_init();
    control(0x037f); push_one();
    x87_save(&parked);
    x87_restore(&parent[1]);
    if (get_control() != 0x0b7f || !pop_one()) return 1;
    x87_restore(&parked);
    if (get_control() != 0x037f || !pop_one()) return 2;
    x87_restore(&parent[0]);
    if (get_control() != 0x077f || !pop_one()) return 3;
    return 0;
}
void _start(void) { int rc = run(); __asm__ volatile("int $0x80" : : "a"(1), "b"(rc) : "memory"); for (;;) {} }
