#include "v86_pic.h"
#include "v86.h"
#include "v86_mem.h"
#include "module.h"

struct v86_session_state v86_session;
void v86_reflect_irq(u32 *frame, u32 irq);

#include "check.h"

/* At #GP entry STI has retired and the following OUT has not. */
__attribute__((section(".guest_code"), used))
static const u8 guest_code[32] = {0xfb, 0xe6, V86_PIC_M_DATA};
__attribute__((section(".guest_stack"), used))
static volatile u16 guest_stack[64];
__attribute__((section(".ivt"), used, retain))
static const u16 guest_ivt[512] = {[0x14 * 2] = 16, [0x14 * 2 + 1] = 0x3000};
struct kdebug kdebug;
int v86_gp_handler(u32 *frame);
int v86_soft_out(u16 port, u8 val) { return 0; }
int v86_bios_is_hle(u32 vector) { return 0; }
void v86_bios_dispatch(u32 *frame, u32 vector) {}
u32 v86_io_in(u16 port, int size) { return 0; }
void v86_io_out(u16 port, int size, u32 value)
{
    check(port == V86_PIC_M_DATA && size == 1 && !(v86_pic_state(1) >> 16),
          "delivery before trapped OUT");
    v86_pic_out(port, value);
}

static void after_sti(void)
{
    u32 frame[V86F_SS + 1] = {0};
    v86_pic_reset();
    v86_pic_set_imr(0, 0x80);
    v86_pic_request(12);
    frame[V86F_EFLAGS] = EFLAGS_VM | EFLAGS_IF;
    frame[V86F_CS] = (u32)guest_code >> 4;
    frame[V86F_EIP] = 1;
    frame[V86F_SS] = (u32)guest_stack >> 4;
    frame[V86F_ESP] = sizeof(guest_stack);
    check(!v86_gp_handler(frame), "guest frame delivery");
    unsigned sp = frame[V86F_ESP] / sizeof(u16);
    check(frame[V86F_EIP] == 16 && frame[V86F_CS] == 0x3000 &&
          sp == 61 && guest_stack[sp] == 3 && guest_stack[sp + 1] == 0x3000 &&
          (guest_stack[sp + 2] & EFLAGS_IF) && !(frame[V86F_EFLAGS] & EFLAGS_IF) &&
          (v86_pic_state(1) >> 16) == 16, "return after STI successor");
}

static void pic(void)
{
    u32 v;
    v86_pic_reset();
    v86_pic_out(V86_PIC_S_CMD, 0x11);
    v86_pic_out(V86_PIC_S_DATA, 0x38);
    v86_pic_out(V86_PIC_S_DATA, 7);
    v86_pic_out(V86_PIC_S_DATA, 1);
    v86_pic_set_imr(0, 0x80);
    v86_pic_set_imr(1, 0x10);
    v86_pic_soft_sound(1);
    v86_pic_soft_sound(0);
    check(v86_pic_candidate(&v) < 0, "both masked");
    v86_pic_set_imr(1, 0);
    check(v86_pic_candidate(&v) < 0, "cascade mask");
    v86_pic_set_imr(0, 0);
    v86_pic_set_imr(1, 0x10);
    check(v86_pic_candidate(&v) < 0, "slave mask");
    v86_pic_set_imr(1, 0);
    v86_pic_request(0);
    check(v86_pic_candidate(&v) == 0, "IRQ0 priority");
    v86_pic_commit(0);
    check(v86_pic_candidate(&v) < 0, "ISR priority");
    v86_pic_out(V86_PIC_M_CMD, 0x20);
    check(v86_pic_candidate(&v) == 12 && v == 0x3c &&
          v86_pic_candidate(&v) == 12, "latched request");
    v86_pic_soft_sound(3);
    v86_pic_commit(12);
    check((v86_pic_state(1) >> 16) == 16 && (v86_pic_state(0) >> 16) == 128,
          "cascade service");
    v86_pic_soft_sound(2);
    v86_pic_out(V86_PIC_S_CMD, 0x64);
    check(v86_pic_candidate(&v) < 0, "partial EOI");
    v86_pic_out(V86_PIC_M_CMD, 0x67);
    check(v86_pic_candidate(&v) == 12, "remaining source");
    v86_pic_commit(12);
    v86_pic_out(V86_PIC_M_CMD, 0x67);
    check(v86_pic_candidate(&v) < 0, "partial master EOI");
    v86_pic_soft_sound(0);
    v86_pic_out(V86_PIC_S_CMD, 0x64);
    check(v86_pic_candidate(&v) < 0, "cleared source");
}
static void auto_eoi(void)
{
    u32 vector;
    v86_pic_reset();
    v86_pic_out(V86_PIC_M_CMD, 0x11);
    v86_pic_out(V86_PIC_M_DATA, 0x08);
    v86_pic_out(V86_PIC_M_DATA, 0x80);
    v86_pic_out(V86_PIC_M_DATA, 0x03);
    v86_pic_request(1);
    v86_pic_commit(1);
    v86_pic_request(1);
    check(v86_pic_candidate(&vector) == 1, "automatic EOI");
}

static void frame_fault(void)
{
    u32 frame[V86I_SS + 1] = {0}, vector;
    v86_pic_reset();
    v86_session.active = 1;
    v86_reflect_irq(frame, 12);
    check(v86_pic_candidate(&vector) == 12, "non-VM latch");
    frame[V86I_EFLAGS] = EFLAGS_VM;
    v86_reflect_irq(frame, 12);
    check(v86_get_exit_reason() != V86_EXIT_FAULT, "IF disabled");
    frame[V86I_EFLAGS] |= EFLAGS_IF;
    frame[V86I_SS] = 0xffff;
    v86_reflect_irq(frame, 12);
    check(v86_get_exit_reason() == V86_EXIT_FAULT && v86_pic_candidate(&vector) == 12 &&
          !(v86_pic_state(0) >> 16) && !(v86_pic_state(1) >> 16), "frame fault commit");
    v86_pic_request(1);
    v86_pic_keyboard_read(0);
    check(v86_pic_candidate(&vector) == 12, "consumed keyboard byte");
    v86_pic_keyboard_read(1);
    check(v86_pic_candidate(&vector) == 1, "queued keyboard byte");
}
void _start(void)
{
    pic();
    auto_eoi();
    after_sti();
    frame_fault();
    passed();
}
