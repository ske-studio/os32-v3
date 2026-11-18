/* 期待の出所: PPC_SQLITE_STACK_DESIGN §6 / PPC_FP_NEWLIB_DESIGN §8 S1. */
#include "arch_kernel_fp.h"
#include "ofw.h"
#include "ppc_boot.h"
#include "ppc_frame.h"
#include "ppc_oea.h"
#include "sqlite_engine.h"

#define S1_VECTOR_EXT 0x500

struct frame { u32 sp, chain, trampoline_sp, caller_sp; };
struct fp_image { unsigned long long fpr[32]; u32 fpscr_high, fpscr; };

extern const unsigned char s1_dec_vector[], s1_dec_vector_end[];
extern const unsigned char s1_ext_vector[], s1_ext_vector_end[];
extern u32 s1_low[], s1_alt[], s1_high[], s1_old[], __stack_top[];
extern void s1_abi_callback(void *), s1_engine_callback(void *);
extern int s1_abi_call(void (*call)(void *), u32 top, struct frame *frame,
                       struct fp_image *after);
extern void s1_seed(const struct fp_image *), s1_sample(struct fp_image *);
extern void sqlite_s1_done(void), sqlite_s1_bad_sp(void);
extern void sqlite_s1_bad_fp(void), sqlite_s1_fail(void);

volatile u32 kctx_irq_depth, kctx_exc_depth;
volatile u32 sqlite_s1_status = 0xffffffff;
static struct pm_escc serial;
static struct fp_image seed, sampled;
static unsigned calls;
static u32 expected_ee;
static struct frame engine_frame;

static void say(const char *line)
{
    unsigned len = 0;
    while (line[len]) ++len;
    pm_escc_write(&serial, line, len);
}

static void finish(const char *line, u32 status, void (*stop)(void))
{
    sqlite_s1_status = status;
    say(line);
    pm_escc_drain(&serial);
    stop();
    for (;;) {}
}

void s1_external_interrupt(void)
{
    finish("S1-FAIL-EXT\r\n", 3, sqlite_s1_fail);
}

#define CHECK(test, name) do { if (!(test)) \
    finish("S1-FAIL-" name "\r\n", 3, sqlite_s1_fail); } while (0)

static u32 msr(void)
{
    u32 value;
    __asm__ volatile("mfmsr %0" : "=r"(value) : : "memory");
    return value;
}

static void set_msr(u32 value)
{
    __asm__ volatile("sync; mtmsr %0; isync" : : "r"(value) : "memory");
}

static void install_irq_vectors(void)
{
    CHECK(!(msr() & PPC_MSR_IP), "VECTOR-PREFIX");
    ppc_write_dec(PPC_DEC_FAR);
    ppc_cache_copy((void *)PPC_VECTOR_DEC, s1_dec_vector,
                   (u32)s1_dec_vector_end - (u32)s1_dec_vector);
    ppc_cache_copy((void *)S1_VECTOR_EXT, s1_ext_vector,
                   (u32)s1_ext_vector_end - (u32)s1_ext_vector);
}

static int matches_seed(unsigned first, int fpscr)
{
    for (unsigned i = first; i < 32; ++i)
        if (sampled.fpr[i] != seed.fpr[i]) return 0;
    return !fpscr || sampled.fpscr == seed.fpscr;
}

static void check_frame(struct frame *frame)
{
    CHECK(frame->sp >= (u32)s1_alt && frame->sp == (u32)s1_high - 16 &&
          !(frame->sp & 15), "ALT-SP");
    CHECK(frame->chain == frame->trampoline_sp && !(frame->chain & 15) &&
          frame->chain >= (u32)s1_old && frame->chain < (u32)__stack_top,
          "BACKCHAIN");
    if (frame->caller_sp)
        CHECK(frame->chain + 16 == frame->caller_sp, "CALLER-FRAME");
}

void s1_abi_body(struct frame *frame)
{
    say("S1-ABI-CALLBACK\r\n");
    check_frame(frame);
}

static void unexpected(void *frame)
{
    (void)frame;
    CHECK(0, "REJECT-CALLBACK");
}

static void rejected(int io, u32 active)
{
    u32 before = msr();
    struct fp_image saved;
    s1_sample(&saved);
    CHECK(sqlite_engine_run(unexpected, 0, io) == 0 &&
          sqlite_engine_active == active && msr() == before, "REJECT-STATE");
    s1_sample(&sampled);
    for (unsigned i = 0; i < 32; ++i)
        CHECK(sampled.fpr[i] == saved.fpr[i], "REJECT-FPR");
    CHECK(sampled.fpscr == saved.fpscr, "REJECT-FPSCR");
}

void s1_engine_body(struct frame *frame)
{
    if (calls++ == 0) say("S1-ENGINE-CALLBACK\r\n");
    CHECK(frame == &engine_frame && sqlite_engine_active == 1 &&
          (msr() & PPC_MSR_FP), "ENGINE-ACTIVE");
    check_frame(frame);
    CHECK((msr() & PPC_MSR_EE) == expected_ee, "CALLBACK-EE");
    rejected(0, 1);
}

static void engine_ok(u32 start_msr)
{
    int result = sqlite_engine_run(s1_engine_callback, &engine_frame, 0);
    u32 after = msr();
    s1_sample(&sampled);
    int restored = matches_seed(0, 1);
    if (after != start_msr || !restored)
        finish("S1-FAIL-FP\r\n", 2, sqlite_s1_bad_fp);
    CHECK(result == 1 && !sqlite_engine_active, "ENGINE-RETURN");
}

void powermac_boot(ppc_ci ci)
{
    struct ofw_env env;
    struct arch_kernel_fp_guard guard;
    struct frame abi = {0};
    ofw_init(ci);
    if (ofw_setup(&env) || pm_escc_prepare(&env, &serial)) ppc_stop();
    const u32 clear = PPC_MSR_FP | PPC_MSR_IR | PPC_MSR_DR | PPC_MSR_EE | PPC_MSR_FE;
    u32 start_msr = msr() & ~clear;
    set_msr(start_msr);
    CHECK(msr() == start_msr, "START-MSR");
    say("S1-BEGIN\r\n");

    CHECK((u32)s1_high - (u32)s1_alt == SQLITE_ENGINE_STACK_SIZE, "STACK-SIZE");
    for (unsigned i = 0; i < 4; ++i) {
        s1_low[i] = 0xa55aa55a ^ i;
        s1_high[i] = 0x5aa55aa5 ^ i;
    }
    for (unsigned i = 0; i < 32; ++i) seed.fpr[i] = 0x3ff0000000000000ULL + i;
    seed.fpscr = 1;
    CHECK(arch_kernel_fp_begin(&guard) == 0, "ABI-GUARD");
    s1_seed(&seed);
    int abi_result = s1_abi_call(s1_abi_callback, (u32)s1_high, &abi, &sampled);
    if (abi_result == 1) finish("S1-FAIL-SP\r\n", 1, sqlite_s1_bad_sp);
    CHECK(abi_result == 0 && abi.sp && matches_seed(14, 0), "ABI-REGS");
    arch_kernel_fp_end(&guard);
    CHECK(msr() == start_msr, "ABI-MSR");

    s1_seed(&seed);
    rejected(0, 0);
    sqlite_engine_stack_top = (u32)s1_high;
    engine_ok(start_msr);
    kctx_irq_depth = 1;
    rejected(0, 0);
    kctx_irq_depth = 0;
    kctx_exc_depth = 1;
    rejected(0, 0);
    kctx_exc_depth = 0;
    rejected(1, 0);
    CHECK(arch_kernel_fp_begin(&guard) == 0, "HELD-BEGIN");
    rejected(0, 0);
    arch_kernel_fp_end(&guard);
    CHECK(msr() == start_msr, "HELD-MSR");
    s1_sample(&sampled);
    CHECK(matches_seed(0, 1), "HELD-FP");
    engine_ok(start_msr);

    install_irq_vectors();
    expected_ee = PPC_MSR_EE;
    set_msr(start_msr | expected_ee);
    CHECK(msr() == (start_msr | expected_ee), "EE-START");
    engine_ok(start_msr | expected_ee);
    CHECK(arch_kernel_fp_begin(&guard) == 0, "EE-HELD");
    CHECK(msr() & PPC_MSR_EE, "EE-BEFORE-REJECT");
    rejected(0, 0);
    CHECK(msr() & PPC_MSR_EE, "EE-REJECT");
    arch_kernel_fp_end(&guard);
    set_msr(start_msr);
    CHECK(calls == 3, "CALL-COUNT");
    for (unsigned i = 0; i < 4; ++i)
        CHECK(s1_low[i] == (0xa55aa55a ^ i) && s1_high[i] == (0x5aa55aa5 ^ i),
              "CANARY");
    finish("S1-PASS\r\n", 0, sqlite_s1_done);
}
