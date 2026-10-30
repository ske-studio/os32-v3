/* E05b: execute the production GP decoder and register/IP helpers. Only port
 * I/O and BIOS dispatch/injection are stubbed; low memory is mapped by _start.
 * No CPU exception entry, physical port access or ROM execution is modeled. */
static volatile enum v86_exit_reason v86_exit_reason;
static volatile u32 v86_gp_since_tick;
static u32 v86_gp_n, v86_gp_fl;
static u32 gp_io_count, gp_port, gp_width, gp_value, gp_int_count, gp_vector;
static u32 gp_next_ip, gp_capture_count, gp_opcode, gp_opsize;
static int gp_hle, gp_inject_ok, gp_abort;

u32 v86_io_in(u16 port, int size)
{
    gp_io_count++; gp_port = port; gp_width = size;
    return 0x76543210;
}
void v86_io_out(u16 port, int size, u32 value)
{
    gp_io_count++; gp_port = port; gp_width = size; gp_value = value;
}
int v86_bios_is_hle(u32 vec) { gp_vector = vec; return gp_hle; }
void v86_bios_dispatch(u32 *frame, u32 vec)
{
    CHECK(gp_hle && vec == gp_vector, "GP HLE routing");
    CHECK(frame[V86F_EIP] == gp_next_ip, "GP advances IP before HLE");
    gp_int_count++;
    frame[V86F_EAX] = 0x12345678;
}
int v86_inject_int(u32 *frame, u32 vec)
{
    CHECK(!gp_hle && vec == gp_vector, "GP IVT routing");
    CHECK(frame[V86F_EIP] == gp_next_ip, "GP advances IP before injection");
    gp_int_count++;
    if (gp_inject_ok) frame[V86F_EIP] = 0x2468;
    return gp_inject_ok;
}
static int gp_capture(int opsize16, u8 opcode)
{
    CHECK(!gp_io_count && !gp_int_count, "GP capture gate before dispatch");
    CHECK((u32)opsize16 == gp_opsize && opcode == gp_opcode, "GP decoded capture arguments");
    gp_capture_count++;
    return gp_abort;
}
#include "gp.inc"

static u8 *gp_prepare(u32 *frame)
{
    kmemset(frame, 0, 18 * sizeof(*frame));
    frame[V86F_CS] = 0x2000;
    frame[V86F_EIP] = 0xffff;
    frame[V86F_EAX] = 0xa1b2c3d4;
    frame[V86F_EDX] = 0x9876abcd;
    frame[V86F_EFLAGS] = 0x23002;
    v86_exit_reason = V86_EXIT_NONE;
    v86_gp_n = v86_gp_since_tick = 0;
    gp_io_count = gp_int_count = gp_capture_count = 0;
    gp_abort = 0;
    gp_opsize = 1;
    kdebug.ops.gcap_insn_abort = gp_capture;
    u8 *pc = v86_ptr(frame[V86F_CS], frame[V86F_EIP]);
    kmemset(pc, 0, 16);
    return pc;
}

static void test_gp_handler(void)
{
    /* Fixed expectations from the original eight opcode cases; include 66h
     * outside capture, which deliberately retains the current AX width. */
    static const struct { u8 op, width, immediate, input; } cases[] = {
        {0xe4, 1, 1, 1}, {0xe5, 2, 1, 1}, {0xec, 1, 0, 1}, {0xed, 2, 0, 1},
        {0xe6, 1, 1, 0}, {0xe7, 2, 1, 0}, {0xee, 1, 0, 0}, {0xef, 2, 0, 0}
    };
    u32 f[18];
    for (u32 i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        for (u32 prefix = 0; prefix < 3; prefix++) {
            u8 *pc = gp_prepare(f);
            if (prefix) pc[0] = prefix == 1 ? 0x2e : 0x66;
            u32 n = prefix != 0;
            pc[n] = gp_opcode = cases[i].op;
            pc[n + 1] = 0x82;
            if (prefix == 2) kdebug.ops.gcap_insn_abort = 0;
            CHECK(v86_gp_handler(f) == 0, "GP I/O returns continue");
            CHECK(f[V86F_EIP] == n + cases[i].immediate, "GP I/O prefixed IP wraps");
            CHECK(gp_io_count == 1 && gp_width == cases[i].width, "GP I/O width and count");
            CHECK(gp_port == (cases[i].immediate ? 0x82 : 0xabcd), "GP immediate/DX port");
            CHECK(gp_capture_count == (prefix != 2), "GP optional capture callback");
            if (cases[i].input)
                CHECK(f[V86F_EAX] == (cases[i].width == 1 ? 0xa1b2c310 : 0xa1b23210), "GP AL/AX preserves upper bits");
            else {
                CHECK(gp_value == (cases[i].width == 1 ? 0xd4 : 0xc3d4), "GP OUT masks AL/AX");
                CHECK(f[V86F_EAX] == 0xa1b2c3d4, "GP OUT preserves EAX");
            }
            CHECK(v86_exit_reason == V86_EXIT_NONE, "GP I/O exit reason");
            CHECK(v86_gp_n == 1 && v86_gp_since_tick == 1 && v86_gp_cs == 0x2000 &&
                  v86_gp_ip == 0xffff && v86_gp_fl == 0x23002, "GP records entry before dispatch");
        }
    }
    for (int route = 0; route < 3; route++) {
        u8 *pc = gp_prepare(f);
        pc[0] = 0x67; pc[1] = gp_opcode = 0xcd; pc[2] = 0x18;
        gp_hle = route == 0; gp_inject_ok = route == 1; gp_next_ip = 2;
        CHECK(v86_gp_handler(f) == (route == 2), "GP INT return");
        CHECK(gp_vector == 0x18 && gp_int_count == 1 && !gp_io_count, "GP INT vector");
        CHECK(f[V86F_EIP] == (route == 1 ? 0x2468 : 2), "GP preserves injected IP");
        CHECK(f[V86F_EAX] == (route == 0 ? 0x12345678 : 0xa1b2c3d4), "GP HLE result");
        CHECK(v86_exit_reason == (route == 2 ? V86_EXIT_FAULT : V86_EXIT_NONE), "GP INT failure reason");
    }
    static const u8 stopped[] = {0xf4, 0x0f, 0x6c, 0x6d, 0x6e, 0x6f};
    for (u32 i = 0; i < sizeof(stopped); i++) {
        u8 *pc = gp_prepare(f);
        pc[0] = 0xf3; pc[1] = gp_opcode = stopped[i];
        CHECK(v86_gp_handler(f) == 1 && f[V86F_EIP] == 0xffff, "GP HLT/default unchanged IP");
        CHECK(v86_exit_reason == (i ? V86_EXIT_UNKNOWN_OP : V86_EXIT_HLT), "GP HLT/default reason");
        CHECK(!gp_io_count && !gp_int_count && gp_capture_count == 1, "GP HLT/default side effects");
    }
    u8 *pc = gp_prepare(f);
    pc[0] = 0x66; pc[1] = gp_opcode = 0xe7; pc[2] = 0x82;
    gp_opsize = 0; gp_abort = 1;
    CHECK(v86_gp_handler(f) == 1 && v86_exit_reason == V86_EXIT_UNKNOWN_OP, "GP capture rejects before OUT");
    CHECK(f[V86F_EIP] == 0xffff && !gp_io_count && gp_capture_count == 1, "GP rejected OUT has no effect");

    pc = gp_prepare(f);
    kmemset(pc, 0x2e, 14); pc[14] = 0xe4;
    CHECK(v86_gp_handler(f) == 1 && v86_exit_reason == V86_EXIT_UNKNOWN_OP, "GP prefix limit");
    CHECK(!gp_capture_count && !gp_io_count && f[V86F_EIP] == 0xffff, "GP prefix limit before capture");
    pc = gp_prepare(f);
    kmemset(pc, 0x2e, 13); pc[13] = gp_opcode = 0xe4; pc[14] = 0x82;
    v86_gp_since_tick = V86_GP_LIMIT - 1;
    CHECK(v86_gp_handler(f) == 0 && f[V86F_EIP] == 14 && gp_port == 0x82, "GP last valid prefix and GP budget");
    pc = gp_prepare(f);
    kmemset(pc, 0x2e, 16); v86_gp_since_tick = V86_GP_LIMIT;
    CHECK(v86_gp_handler(f) == 1 && v86_exit_reason == V86_EXIT_GP_LIMIT, "GP budget before prefix limit");
    CHECK(!gp_capture_count && !gp_io_count, "GP budget prevents dispatch");
    gp_prepare(f); f[V86F_CS] = 0xffff; v86_gp_since_tick = V86_GP_LIMIT;
    CHECK(v86_gp_handler(f) == 1 && v86_exit_reason == V86_EXIT_FAULT, "GP fetch range before budget");
    CHECK(!gp_capture_count && !gp_io_count, "GP fetch prevents dispatch");
    gp_prepare(f); f[V86F_CS] = 0xffff; f[V86F_EIP] = 0;
    pc = v86_ptr(0xffff, 0); pc[0] = gp_opcode = 0xf4;
    CHECK(v86_gp_handler(f) == 1 && v86_exit_reason == V86_EXIT_HLT, "GP ROM fetch at last valid address");
    gp_prepare(f); f[V86F_CS] = 0xffff; f[V86F_EIP] = 1;
    CHECK(v86_gp_handler(f) == 1 && v86_exit_reason == V86_EXIT_FAULT, "GP fetch crosses mapped end");
    kdebug.ops.gcap_insn_abort = 0;
    SAY("GREEN GP actual handler: I/O, prefix/IP, INT, HLT/default, fetch/budget/capture order");
}
