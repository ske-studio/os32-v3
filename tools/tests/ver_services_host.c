#include "support.h"
#include "sqlite_engine.h"
#include "services_source.c"
int services_public;
enum services_stage services_stage;
u32 sqlite_engine_stack_top;
static u8 stack[SQLITE_ENGINE_STACK_SIZE];
static const char *const expected[] = {
    "STARTING", "STARTING", "STARTING", "STARTING", "STARTING",
    "SERVICES_READY", "MINIMAL", "MINIMAL_QUARANTINED", "SQLITE_ONLY"
};
static void snapshot(u32 used, int public_value, const char *state)
{
    struct { u32 before; ServicesVerState value; u32 after; } guarded;
    unsigned int i;
    guarded.before = 0x12345678;
    guarded.after = 0x87654321;
    for (i = 0; i < sizeof(guarded.value); i++) ((u8 *)&guarded.value)[i] = 0xcc;
    services_public = public_value;
    services_ver_state(&guarded.value);
    CHECK(guarded.before == 0x12345678 && guarded.after == 0x87654321);
    CHECK(guarded.value.stack_used == used);
    CHECK(guarded.value.stack_size == SQLITE_ENGINE_STACK_SIZE);
    CHECK(guarded.value.public == (u32)public_value);
    CHECK(equal(guarded.value.state, state));
    for (i = 0; i < sizeof(guarded.value.state) && guarded.value.state[i]; i++) {}
    CHECK(i < sizeof(guarded.value.state));
    CHECK(services_public == public_value);
}
void _start(void)
{
    unsigned int stage, i;
    snapshot(0, 0, "STARTING"); /* no stack / services not started */
    sqlite_engine_stack_top = (u32)(stack + sizeof(stack));
    for (i = 0; i < sizeof(stack); i++) stack[i] = SQLITE_STACK_MARK;
    for (stage = 0; stage < sizeof(expected)/sizeof(expected[0]); stage++) {
        services_stage = (enum services_stage)stage;
        snapshot(0, 0, expected[stage]);
        snapshot(0, 1, expected[stage]);
        CHECK((unsigned int)services_stage == stage);
    }
    services_stage = SERVICES_READY;
    stack[sizeof(stack)-1] = 0;
    snapshot(1, 1, "SERVICES_READY");
    stack[sizeof(stack)-4096] = 0;
    snapshot(4096, 7, "SERVICES_READY");
    stack[0] = 0;
    snapshot(SQLITE_ENGINE_STACK_SIZE, 0, "SERVICES_READY");
    for (i = 0; i < sizeof(stack); i++) stack[i] = SQLITE_STACK_MARK;
    snapshot(SQLITE_ENGINE_STACK_SIZE, 0, "SERVICES_READY"); /* saved peak */
    sqlite_engine_stack_top = 0;
    snapshot(0, 0, "SERVICES_READY");
    say("PASS services: all states, public, stack peak, NUL, canaries; no output from getter\n");
    die(0);
}
