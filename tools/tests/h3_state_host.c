#include "userland/tests/h3/protocol.h"
#include "userland/tests/h3/state.inc"
static void finish(int code)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(code) : "memory");
    for (;;) { }
}
#define CHECK(expr) do { if (!(expr)) finish(__LINE__); } while (0)
void _start(void)
{
    H3Block b = {0};
    unsigned int mode, phase;
    CHECK(!h3_identity(&b));
    b.owner = 17;
    CHECK(!h3_identity(&b));
    b.owner = 0; b.generation = 91;
    CHECK(!h3_identity(&b));
    b.owner = 17;
    CHECK(h3_identity(&b) && b.phase == H3_IDENTIFIED);
    for (phase = H3_INIT; phase <= H3_ERROR; phase++) {
        if (phase == H3_RESUMED) continue;
        b.phase = phase; b.arm = 1; b.mode = H3_PF; b.consumed = 0;
        CHECK(h3_consume(&b) == 0 && b.arm == 1 && b.consumed == 0);
    }
    for (mode = H3_PF; mode <= H3_KAPI_LOOP; mode++) {
        b.phase = H3_RESUMED; b.arm = 0; b.mode = mode; b.consumed = 0;
        CHECK(h3_consume(&b) == 0);
        b.arm = 2;
        CHECK(h3_consume(&b) == 0);
        b.arm = 1;
        CHECK(h3_consume(&b) == mode);
        CHECK(b.arm == 0 && b.consumed == 1 && b.phase == H3_ARMED);
        b.phase = H3_RESUMED; b.arm = 1;
        CHECK(h3_consume(&b) == 0 && b.consumed == 1);
    }
    b.phase = H3_RESUMED; b.arm = 1; b.mode = 0; b.consumed = 0;
    CHECK(h3_consume(&b) == 0 && b.phase == H3_ERROR);
    b.phase = H3_RESUMED; b.mode = 7;
    CHECK(h3_consume(&b) == 0 && b.phase == H3_ERROR);
    finish(0);
}
