/* P0 executable contract examples, NOT the future P2 runner. Real decoder,
 * fake runs + adapter: exercise pair supplementation, shared waits and undo. */
#include "hwdev/gf/internal.h"

static void fail(void) __attribute__((noreturn));
static void fail(void)
{
    __asm__ volatile("int $0x80" : : "a"(1), "b"(1) : "memory");
    for (;;) { }
}
#define CHECK(x) do { if (!(x)) { \
    static const char msg[] = "FAIL GF contract: " #x "\n"; \
    __asm__ volatile("int $0x80" : : "a"(4), "b"(1), "c"(msg), \
                     "d"(sizeof(msg) - 1) : "memory"); fail(); \
} } while (0)
#define ARRAY_COUNT(a) (sizeof(a) / sizeof((a)[0]))
#define TEST_TIME_US 2500U
#define TEST_UNDO_OLD 0xfaceffffU
#define TEST_SELECT (RECOVER_PRMCIO + RECOVER_CRTC)
#define TEST_DATA (TEST_SELECT + 1U)
#define TEST_INDEX 0x3cU
#define TEST_OTHER_INDEX 0x20U
#define TEST_LATCH_OLD 0x55U
#define TEST_WRITES_MAX (GF_INIT_SAVE_MAX * 4U)
struct fake_bus {
    u32 now, points, clocks, waits, waited, chunks[TEST_WRITES_MAX];
    u32 cancel_at, clock_fail_at, wait_fail_at, write_fail_at;
    u32 writes, write_offsets[TEST_WRITES_MAX];
    u32 reset, legacy, pll, coeff, index, latch[GF_INIT_COUNT_MAX];
};
static int point(void *opaque)
{
    struct fake_bus *b = opaque;
    return ++b->points == b->cancel_at ? -1 : 0;
}
static int clock_us(void *opaque, u32 *now)
{
    struct fake_bus *b = opaque;
    if (++b->clocks == b->clock_fail_at) return -1;
    *now = b->now;
    return 0;
}
static int wait_us(void *opaque, u32 us)
{
    struct fake_bus *b = opaque;
    CHECK(b->waits < ARRAY_COUNT(b->chunks));
    b->chunks[b->waits++] = us;
    if (b->waits == b->wait_fail_at) return -1;
    b->waited += us;
    b->now += us;
    return 0;
}
static u32 *cell(struct fake_bus *b, u32 off)
{
    switch (off) {
    case RECOVER_PBUS184C: return &b->reset;
    case RECOVER_PBUS1850: return &b->legacy;
    case GF_PLL_VIDEO: return &b->pll;
    case GF_PLL_VIDEO_COEFF: return &b->coeff;
    case TEST_SELECT: return &b->index;
    case TEST_DATA: return &b->latch[b->index];
    default: CHECK(false); return NULL;
    }
}
static int read_reg(void *opaque, u32 bar, u32 off, u32 width, u32 *value)
{
    CHECK(bar == KPEEK_NV_BAR_MMIO && (width == 1 || width == 4));
    *value = *cell(opaque, off);
    return 0;
}
static int write_reg(void *opaque, u32 bar, u32 off, u32 width, u32 value)
{
    struct fake_bus *b = opaque;
    CHECK(bar == KPEEK_NV_BAR_MMIO && (width == 1 || width == 4));
    CHECK(b->writes < ARRAY_COUNT(b->write_offsets));
    b->write_offsets[b->writes++] = off;
    if (b->writes == b->write_fail_at) return -1;
    *cell(b, off) = value;
    return 0;
}
static int exec_point(struct gf_init_context *c)
{
    u32 now;
    if (c->io.point(c->io.opaque) || c->io.timing.clock_us(c->io.opaque, &now)) return -1;
    c->elapsed_us = now - c->started_us;
    return c->elapsed_us >= GF_INIT_EXEC_US ? -1 : 0;
}
int gf_init_wait(struct gf_init_context *c, u32 us)
{
    if (us > GF_INIT_WAIT_MAX - c->wait_us) return -1;
    c->wait_us += us;
    while (us) {
        u32 part = us > GF_INIT_WAIT_CHUNK_US ? GF_INIT_WAIT_CHUNK_US : us;
        if (exec_point(c) || c->io.timing.wait_us(c->io.opaque, part) || exec_point(c)) return -1;
        us -= part;
    }
    return 0;
}
int gf_init_save(struct gf_init_context *c, u32 bar, u32 off, u32 width, u32 old)
{
    if (c->saved_count == GF_INIT_SAVE_MAX) return -1;
    c->saved[c->saved_count++] = (struct gf_init_saved){
        bar, off, width, old, RECOVER_NONE, 0};
    return 0;
}
int gf_init_save_indexed(struct gf_init_context *c, u32 bar, u32 select_off,
                         u32 data_off, u32 index, u32 old)
{
    if (gf_init_save(c, bar, data_off, 1, old)) return -1;
    c->saved[c->saved_count - 1].select_offset = select_off;
    c->saved[c->saved_count - 1].select_value = index;
    return 0;
}
void gf_init_commit(struct gf_init_context *c) { c->saved_count = 0; }
static int restore(void *opaque, struct gf_init_context *c)
{
    int rc = 0;
    for (u32 n = c->saved_count; n; n--) {
        const struct gf_init_saved *s = &c->saved[n - 1];
        if (s->select_offset != RECOVER_NONE &&
            c->io.write(opaque, s->bar, s->select_offset, 1, s->select_value)) {
            rc = -1;
            continue; /* Still attempt every older entry. */
        }
        if (c->io.write(opaque, s->bar, s->offset, s->width, s->old)) rc = -1;
    }
    if (!rc) c->saved_count = 0;
    return rc;
}
static struct gf_init_context context(struct fake_bus *b)
{
    return (struct gf_init_context){.io = {
        .opaque = b, .read = read_reg, .write = write_reg,
        .timing = {clock_us, wait_us}, .point = point, .restore = restore}};
}
/* Fake runs deliberately depend only on internal.h, as independent P2 files
 * will. No old plan executor or expansion is included. */
int gf_op_repeat_run(struct gf_init_context *c, const struct gfinit_instruction *i)
{
    if (c->repeats == GF_INIT_REPEAT_DEPTH || i->end_repeat == RECOVER_NONE) return -1;
    c->repeat[c->repeats++] = (struct gfinit_exec_frame){c->pc, i->end_repeat, i->count};
    return 0;
}
int gf_op_end_repeat_run(struct gf_init_context *c, const struct gfinit_instruction *i)
{
    if (!c->repeats || c->repeat[c->repeats - 1].ctx != i->pc) return -1;
    struct gfinit_exec_frame *f = &c->repeat[c->repeats - 1];
    if (--f->count) c->pc = f->pc;
    else c->repeats--;
    return 0;
}
int gf_op_time_run(struct gf_init_context *c, const struct gfinit_instruction *i)
{
    return c->skip ? 0 : gf_init_wait(c, i->value);
}
int gf_op_reset_run(struct gf_init_context *c, const struct gfinit_instruction *i)
{
    u32 old;
    void *b = c->io.opaque;
    if (c->io.read(b, KPEEK_NV_BAR_MMIO, RECOVER_PBUS184C, 4, &old) ||
        gf_init_save(c, KPEEK_NV_BAR_MMIO, RECOVER_PBUS184C, 4, old) ||
        c->io.write(b, KPEEK_NV_BAR_MMIO, RECOVER_PBUS184C, 4, old & GF_INIT_RESET_KEEP) ||
        c->io.write(b, KPEEK_NV_BAR_MMIO, i->target, 4, i->mask) ||
        gf_init_wait(c, GF_INIT_RESET_US) ||
        c->io.write(b, KPEEK_NV_BAR_MMIO, i->target, 4, i->value) ||
        c->io.write(b, KPEEK_NV_BAR_MMIO, RECOVER_PBUS184C, 4, old) ||
        c->io.write(b, KPEEK_NV_BAR_MMIO, RECOVER_PBUS1850, 4, 0)) return -1;
    gf_init_commit(c);
    return 0;
}
const struct gf_op_dispatch gf_op_dispatch[] = {
    {GF_INIT_REPEAT, gf_op_repeat_run}, {GF_INIT_END_REPEAT, gf_op_end_repeat_run},
    {GF_INIT_TIME, gf_op_time_run}, {GF_INIT_RESET, gf_op_reset_run}
};
const u32 gf_op_dispatch_count = ARRAY_COUNT(gf_op_dispatch);
/* Test runner's prepass uses the REAL instruction parser. Match nested
 * instructions, not bytes; don't carry old effect/capture/plan metadata. */
static int decode(const u8 *rom, u32 size, struct gfinit_instruction *ins, u32 *count)
{
    u32 stack[GF_INIT_REPEAT_DEPTH], depth = 0;
    *count = 0;
    for (u32 pc = 0; pc < size;) {
        u32 id = (*count)++;
        struct gfinit_instruction *i = &ins[id];
        if (gfinit_decode_instruction(rom, size, pc, i)) return -1;
        if (i->opcode == GF_INIT_REPEAT) {
            if (depth == GF_INIT_REPEAT_DEPTH) return -1;
            stack[depth++] = id;
        } else if (i->opcode == GF_INIT_END_REPEAT) {
            if (!depth) return -1;
            ins[stack[--depth]].end_repeat = pc;
        }
        pc += i->length;
    }
    return depth ? -1 : 0;
}
static int run(struct gf_init_context *c, const struct gfinit_instruction *ins, u32 count)
{
    while (c->pc < c->rom_size) {
        const struct gfinit_instruction *i = NULL;
        for (u32 n = 0; n < count; n++) if (ins[n].pc == c->pc) i = &ins[n];
        if (!i || ++c->steps > GF_INIT_SCHEDULE || exec_point(c)) return -1;
        c->pc = i->pc + i->length;
        int rc = -1;
        for (u32 n = 0; n < gf_op_dispatch_count; n++)
            if (gf_op_dispatch[n].opcode == i->opcode) rc = gf_op_dispatch[n].run(c, i);
        if (rc || exec_point(c)) return -1;
    }
    return c->repeats ? -1 : 0;
}
static void repeat_test(void)
{
    /* 2 * 3 TIME(2500); both frames must name their own END_REPEAT PC. */
    const u8 rom[] = {GF_INIT_REPEAT, 2, GF_INIT_REPEAT, 3,
        GF_INIT_TIME, TEST_TIME_US & 0xff, TEST_TIME_US >> 8,
        GF_INIT_END_REPEAT, GF_INIT_END_REPEAT};
    struct gfinit_instruction ins[ARRAY_COUNT(rom)], single;
    u32 count;
    CHECK(!gfinit_decode_instruction(rom, sizeof(rom), 0, &single));
    CHECK(single.end_repeat == RECOVER_NONE);
    CHECK(!decode(rom, sizeof(rom), ins, &count));
    CHECK(ins[0].end_repeat == sizeof(rom) - 1 && ins[1].end_repeat == sizeof(rom) - 2);
    CHECK(ins[2].end_repeat == RECOVER_NONE && ins[2].value == TEST_TIME_US);
    struct fake_bus b = {0};
    struct gf_init_context c = context(&b);
    c.rom = rom; c.rom_size = sizeof(rom);
    CHECK(!run(&c, ins, count) && !c.repeats && c.wait_us == 6 * TEST_TIME_US);
    CHECK(b.waited == c.wait_us && b.waits == 18);
    b = (struct fake_bus){0}; c = context(&b);
    c.rom = rom; c.rom_size = sizeof(rom); c.skip = true;
    CHECK(!run(&c, ins, count) && !c.repeats && !b.waits);
    c.pc = 2;
    CHECK(!gf_op_repeat_run(&c, &ins[0]));
    CHECK(c.repeat[0].pc == 2 && c.repeat[0].ctx == ins[0].end_repeat && c.repeat[0].count == 2);
    CHECK(gf_op_end_repeat_run(&c, &ins[3]) == -1); /* Inner END isn't this frame's. */
    CHECK(decode(rom, sizeof(rom) - 1, ins, &count) == -1);
    const u8 unmatched[] = {GF_INIT_END_REPEAT};
    CHECK(decode(unmatched, sizeof(unmatched), ins, &count) == -1);
}
static void wait_test(void)
{
    struct fake_bus b = {0};
    struct gf_init_context c = context(&b);
    struct gfinit_instruction time = {.value = TEST_TIME_US};
    CHECK(!gf_op_time_run(&c, &time) && c.wait_us == TEST_TIME_US);
    CHECK(b.waits == 3 && b.chunks[0] == GF_INIT_WAIT_CHUNK_US &&
          b.chunks[1] == GF_INIT_WAIT_CHUNK_US && b.chunks[2] == TEST_TIME_US % GF_INIT_WAIT_CHUNK_US);
    CHECK(b.points == 6 && b.clocks == 6 && c.elapsed_us == TEST_TIME_US);
    CHECK(!gf_op_time_run(&c, &time) && c.wait_us == 2 * TEST_TIME_US && b.waited == c.wait_us);
    b = (struct fake_bus){.cancel_at = 2}; c = context(&b);
    CHECK(gf_op_time_run(&c, &time) == -1 && c.wait_us == TEST_TIME_US);
    CHECK(b.waits == 1 && b.waited == GF_INIT_WAIT_CHUNK_US && b.points == 2);
    b = (struct fake_bus){.clock_fail_at = 2}; c = context(&b);
    CHECK(gf_op_time_run(&c, &time) == -1 && b.waits == 1 && c.wait_us == TEST_TIME_US);
    b = (struct fake_bus){.wait_fail_at = 1}; c = context(&b);
    CHECK(gf_op_time_run(&c, &time) == -1 && b.waits == 1 && !b.waited && c.wait_us == TEST_TIME_US);
    b = (struct fake_bus){.now = GF_INIT_EXEC_US - GF_INIT_WAIT_CHUNK_US}; c = context(&b);
    CHECK(gf_op_time_run(&c, &time) == -1 && b.waits == 1 && c.elapsed_us == GF_INIT_EXEC_US);
    b = (struct fake_bus){0}; c = context(&b); c.wait_us = GF_INIT_WAIT_MAX;
    CHECK(gf_init_wait(&c, 1) == -1 && c.wait_us == GF_INIT_WAIT_MAX && !b.points && !b.waits);
    CHECK(!gf_init_wait(&c, 0) && !b.points && !b.clocks);
}
static void reset_test(void)
{
    struct gfinit_instruction i = {.target = GF_PLL_VIDEO, .mask = 1, .value = 2};
    struct fake_bus b = {.reset = TEST_UNDO_OLD, .cancel_at = 2};
    struct gf_init_context c = context(&b);
    c.skip = true; /* RESET is forced even while skip. */
    CHECK(gf_op_reset_run(&c, &i) == -1 && c.saved_count == 1);
    CHECK(b.reset == (TEST_UNDO_OLD & GF_INIT_RESET_KEEP) && b.waits == 1);
    CHECK(c.wait_us == GF_INIT_RESET_US && b.waited == GF_INIT_RESET_US);
    b.cancel_at = b.points + 1; /* Cleanup ignores cancellation/deadline. */
    b.now = GF_INIT_EXEC_US;
    CHECK(!c.io.restore(c.io.opaque, &c) && b.reset == TEST_UNDO_OLD && !c.saved_count);
    b = (struct fake_bus){.reset = TEST_UNDO_OLD}; c = context(&b);
    CHECK(!gf_op_reset_run(&c, &i) && !c.saved_count && b.reset == TEST_UNDO_OLD && b.pll == i.value);
    u32 writes = b.writes;
    CHECK(!c.io.restore(c.io.opaque, &c) && b.writes == writes);
}
static void saves_test(void)
{
    struct fake_bus b = {.index = TEST_OTHER_INDEX};
    b.latch[TEST_INDEX] = TEST_LATCH_OLD;
    struct gf_init_context c = context(&b);
    CHECK(!gf_init_save(&c, KPEEK_NV_BAR_MMIO, TEST_SELECT, 1, b.index));
    CHECK(!write_reg(&b, KPEEK_NV_BAR_MMIO, TEST_SELECT, 1, TEST_INDEX));
    CHECK(!gf_init_save_indexed(&c, KPEEK_NV_BAR_MMIO, TEST_SELECT, TEST_DATA, TEST_INDEX, b.latch[TEST_INDEX]));
    CHECK(!write_reg(&b, KPEEK_NV_BAR_MMIO, TEST_DATA, 1, 0));
    CHECK(!write_reg(&b, KPEEK_NV_BAR_MMIO, TEST_SELECT, 1, TEST_OTHER_INDEX));
    CHECK(!c.io.restore(&b, &c) && b.index == TEST_OTHER_INDEX && b.latch[TEST_INDEX] == TEST_LATCH_OLD);
    CHECK(b.write_offsets[3] == TEST_SELECT && b.write_offsets[4] == TEST_DATA && b.write_offsets[5] == TEST_SELECT);
    /* Fake PLL: successful configuration survives stop; failed partial PLL
     * restores both destinations in reverse, without touching baseline. */
    for (u32 success = 0; success <= 1; success++) {
        b = (struct fake_bus){.pll = 1, .coeff = 2}; c = context(&b);
        CHECK(!gf_init_save(&c, KPEEK_NV_BAR_MMIO, GF_PLL_VIDEO, 4, b.pll));
        CHECK(!write_reg(&b, KPEEK_NV_BAR_MMIO, GF_PLL_VIDEO, 4, 3));
        CHECK(!gf_init_save(&c, KPEEK_NV_BAR_MMIO, GF_PLL_VIDEO_COEFF, 4, b.coeff));
        CHECK(!write_reg(&b, KPEEK_NV_BAR_MMIO, GF_PLL_VIDEO_COEFF, 4, 4));
        if (success) gf_init_commit(&c);
        CHECK(!c.io.restore(&b, &c));
        CHECK(b.pll == (success ? 3U : 1U) && b.coeff == (success ? 4U : 2U));
        CHECK(b.writes == (success ? 2U : 4U));
        if (!success) CHECK(b.write_offsets[2] == GF_PLL_VIDEO_COEFF && b.write_offsets[3] == GF_PLL_VIDEO);
    }
    b = (struct fake_bus){.write_fail_at = 1}; c = context(&b);
    CHECK(!gf_init_save(&c, KPEEK_NV_BAR_MMIO, GF_PLL_VIDEO, 4, 1));
    CHECK(!gf_init_save(&c, KPEEK_NV_BAR_MMIO, GF_PLL_VIDEO_COEFF, 4, 2));
    CHECK(c.io.restore(&b, &c) == -1 && b.writes == 2 && b.pll == 1 && c.saved_count == 2);
    c = context(&b);
    for (u32 n = 0; n < GF_INIT_SAVE_MAX; n++) CHECK(!gf_init_save(&c, 0, GF_PLL_VIDEO, 4, n));
    CHECK(gf_init_save(&c, 0, GF_PLL_VIDEO, 4, 0) == -1 && c.saved_count == GF_INIT_SAVE_MAX);
    CHECK(c.saved[GF_INIT_SAVE_MAX - 1].old == GF_INIT_SAVE_MAX - 1);
}
void gf_init_contract_test(void)
{
    repeat_test();
    wait_test();
    reset_test();
    saves_test();
}
