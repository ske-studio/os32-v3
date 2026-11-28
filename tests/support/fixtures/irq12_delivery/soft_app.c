/* Exercise the command with a timed guest and a PCM sink that keeps its handle. */
#define main v86_command_main
#include "userland/cmds/v86.c"
#undef main
#include "check.h"

static const char *expected_path, *expected_second;
static int primed, retry_prime, yields;
static i32 yield(void) { ++yields; return 0; }
static int begin_rc, open_rc, fault, running, stream, ends, fallback, pumps, turns, keys;
static int begin(const V86_Begin *b, u32 *sid)
{
    check(b->flags == 1 && b->size == sizeof *b && b->path[0], "begin request");
    if (begin_rc) return begin_rc;
    check(!running && !stream, "command restart");
    running = 1;
    turns = pumps = keys = 0;
    *sid = 11;
    return 0;
}
static int run(u32 sid, const V86_RunIn *in, V86_Exit *ex)
{
    check(sid == 11 && stream && pumps == turns + 1, "pump on empty slice");
    check(primed, "pre-run reserve");
    primed = 0;
    ++turns;
    ex->reason = turns < 4 ? 1 : fault ? 5 : 4;
    ex->code = turns < 4 ? 8 : fault ? 6 : 5;
    return 0;
}
static int drain(u32 sid, SND2_FmEv *out, u32 n, u32 *seq)
{
    if (turns != 1 && turns != 3) return 0;
    *out = (SND2_FmEv){1, 0, 0x28, turns == 1 ? 0x10 : 0, turns, keys + 1};
    *seq = keys + 1;
    return 1;
}
static int end(u32 sid)
{
    check(running && !stream, "stream release before end");
    running = 0;
    ++ends;
    return 0;
}
static int boot2(const char *path, const char *second) {
    if (expected_path)
        check(path == expected_path && second == expected_second, "fallback path pair");
    ++fallback;
    return 1;
}

int sndl_init(KernelAPI *kapi) { return 0; }
int sndl_chip_open(const SNDL_ChipReq *r, u32 *id, SNDL_ChipGot *got)
{
    if (open_rc) return open_rc;
    check(running && !stream && r->chip == SND2_CHIP_OPNA, "soft open");
    stream = *id = 1;
    got->backend = SNDL_SOFT;
    return 0;
}
int sndl_prime_slice(void)
{
    check(stream && pumps == turns + 1, "pre-run reserve");
    if (retry_prime) { retry_prime = 0; return OS32_ERR_AGAIN; }
    primed = 1;
    return 0;
}
int sndl_pump(void) { check(running && stream, "pump handle"); ++pumps; return 0; }
int sndl_chip_write(u32 chip, const SND2_RegW *w, u32 n, u32 at)
{
    check(n == 1 && w->bank == 0 && w->reg == 0x28 &&
          w->val == (keys ? 0 : 0x10) && at == (keys ? 3 : 1), "command key sequence");
    ++keys;
    return 1;
}
int sndl_chip_close(u32 chip) { check(keys == 2 && pumps == 5, "final drain"); stream = 0; return 0; }
void _start(void)
{
    static KernelAPI table = {.sys_yield = yield, .v86s_begin = begin, .v86s_run = run, .v86s_fm_drain = drain,
                              .v86s_end = end, .v86_boot2 = boot2};
    api = &table;
    begin_rc = OS32_ERR_NOSYS;
    check(boot_sound("fmtone.fdi", 0) == 1 && fallback == 1, "legacy fallback");
    begin_rc = OS32_ERR_BUSY;
    check(boot_sound("fmtone.fdi", 0) == OS32_ERR_BUSY && fallback == 1, "busy fallback");
    begin_rc = 0;
    open_rc = OS32_ERR_IO;
    check(boot_sound("fmtone.fdi", 0) == open_rc && ends == 1 && fallback == 1, "open failure");
    check(!strcmp(boot_stage, "sndl_chip_open"), "sound failure stage");
    open_rc = 0;
    retry_prime = 1;
    for (fault = 0; fault <= 1; ++fault)
        check(boot_sound("fmtone.fdi", 0) == (fault ? 6 : 5), "command exit");
    check(yields == 1, "resume after temporary full");
    check(ends == 3 && !running && !stream, "command cleanup");
    char path[65];
    for (unsigned i = 0; i < 64; ++i) path[i] = 'a';
    path[64] = 0;
    expected_path = path;
    expected_second = "drive2.fdi";
    check(boot_sound(expected_path, expected_second) == 1 && fallback == 2, "long primary");
    expected_path = "drive1.fdi";
    expected_second = path;
    check(boot_sound(expected_path, expected_second) == 1 && fallback == 3, "long secondary");
    fault = 1;
    path[63] = 0;
    check(boot_sound(path, path) == 6 && fallback == 3, "63 byte paths");
    passed();
}
