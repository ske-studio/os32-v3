#define GF5_HOST_UNIT 8
#include "gf5_fake.h"
#include "../../hwdev/gf/internal.h"

/* BIT fields: nouveau nvkm/subdev/bios/bit.c:31-42 and init.c:359-407.
 * The base and gaps are arbitrary fixture placement, not hardware limits. */
enum {
    FIX_BIT_BASE = 16, FIX_BIT_HEADER_SIZE = 12, FIX_BIT_ENTRY_SIZE = 6,
    FIX_BIT_ENTRY = FIX_BIT_BASE + FIX_BIT_HEADER_SIZE,
    FIX_BIT_PAYLOAD = 80, FIX_MEMORY_PC = 180,
    FIX_MEMORY_POINTER_SIZE = 2, FIX_ROM_SIZE = 256
};

static struct gf5_host host;
static struct gf4_state state;
static struct gf5_host_event events[32];
static u8 arena[sizeof(struct gf_init_program) + 1024] __attribute__((aligned(16)));
static u8 rom[FIX_ROM_SIZE];
static struct gf_init_program program;

static void fixture(void)
{
    gf5_host_init(&host, &state);
    host.events = events; host.cevents = 32;
    gf5_host_arena(&host, arena, sizeof(arena));
    gf2_host_memset(rom, 0, sizeof(rom));
    rom[FIX_BIT_BASE] = 0xff; rom[FIX_BIT_BASE + 1] = 0xb8;
    rom[FIX_BIT_BASE + 2] = 'B'; rom[FIX_BIT_BASE + 3] = 'I';
    rom[FIX_BIT_BASE + 4] = 'T';
    rom[FIX_BIT_BASE + 8] = FIX_BIT_HEADER_SIZE;
    rom[FIX_BIT_BASE + 9] = FIX_BIT_ENTRY_SIZE;
    rom[FIX_BIT_BASE + 10] = 1;
    rom[FIX_BIT_ENTRY] = 'M'; rom[FIX_BIT_ENTRY + 1] = 1;
    rom[FIX_BIT_ENTRY + 2] = FIX_MEMORY_POINTER_SIZE;
    rom[FIX_BIT_ENTRY + 4] = FIX_BIT_PAYLOAD;
    /* BIT M + 0 is the LE16 script PC: nouveau ramnv40.c:154-156. */
    rom[FIX_BIT_PAYLOAD] = FIX_MEMORY_PC & 0xff;
    rom[FIX_BIT_PAYLOAD + 1] = FIX_MEMORY_PC >> 8;
    rom[FIX_MEMORY_PC] = GF_INIT_DONE;
    gf2_host_memset(&program, 0, sizeof(program));
    program.count = 1; program.insns[0].pc = FIX_MEMORY_PC;
    program.insns[0].opcode = GF_INIT_DONE;
}
static void test(void)
{
    fixture();
    assert(gf5_rom_acquire(&host.ctx, rom, sizeof(rom), &program)==GF5_OK);
    assert(host.live_allocs==2 && host.ctx.rom.size==sizeof(rom));
    assert(host.ctx.rom.bytes!=rom && host.ctx.rom.memory_program!=&program);
    assert(host.ctx.rom.memory_pc==FIX_MEMORY_PC && host.ctx.rom.bytes[FIX_MEMORY_PC]==GF_INIT_DONE);
    gf2_host_memset(rom, 0, sizeof(rom));
    gf2_host_memset(&program, 0, sizeof(program));
    assert(host.ctx.rom.bytes[FIX_MEMORY_PC]==GF_INIT_DONE && host.ctx.rom.memory_program->count==1);
    assert(host.ctx.rom.memory_program->insns[0].pc==FIX_MEMORY_PC);
    gf5_rom_release(&host.ctx);
    assert(!host.live_allocs && !host.ctx.rom.bytes && !host.ctx.rom.memory_program);
    assert(!host.ctx.rom.size && !host.ctx.rom.memory_pc);
    gf5_rom_release(&host.ctx);

    fixture();
    gf5_host_fail_at(&host,F5_ALLOC,2,GF5_ENOMEM);
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_ENOMEM);
    assert(host.calls[F5_ALLOC]==2 && host.calls[F5_FREE]==1 && !host.live_allocs);
    assert(!host.ctx.rom.bytes && !host.ctx.rom.memory_program && !host.ctx.rom.size);
    assert(host.ctx.last_rc==GF5_ENOMEM && host.ctx.source_rc==GF5_ENOMEM);

    fixture();
    gf5_host_fail_at(&host,F5_ALLOC,1,GF5_ENOMEM);
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_ENOMEM);
    assert(host.calls[F5_ALLOC]==1 && !host.calls[F5_FREE] && !host.live_allocs);
    fixture(); rom[FIX_BIT_ENTRY]='X';
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),0)==GF5_OK);
    assert(host.live_allocs==1 && !host.ctx.rom.memory_program && !host.ctx.rom.memory_pc);
    gf5_rom_release(&host.ctx);
    fixture(); rom[FIX_BIT_ENTRY + 2]=FIX_MEMORY_POINTER_SIZE - 1;
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_EBIOS);
    assert(!host.calls[F5_ALLOC] && !host.live_allocs);
}
/* The reference keeps calc/prog private (nouveau clk/base.c:195-198).
 * OS32's public plans must lose that provenance at every ROM boundary. */
static void seed_guard(u32 generation)
{
    const struct gf5_clock request = {0};
    const struct gf5_plan plan = {0};
    gf5_host_plan_seed(&host, &request, &plan, host.ctx.ch->epoch, generation);
    assert(host.ctx.plan_guard.valid);
}
static void invalid_before_callback(struct gf5_host *h,
                                    const struct gf5_host_event *e)
{
    if (e->op==F5_ALLOC || e->op==F5_FREE)
        assert(!h->ctx.plan_guard.valid);
}
static void rom_guard(void)
{
    fixture();
    host.observe=invalid_before_callback;
    seed_guard(0);
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_OK);
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==1);
    const u8 *first=host.ctx.rom.bytes;
    seed_guard(1);
    gf5_rom_release(&host.ctx);
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==2);
    seed_guard(2);
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_OK);
    assert(host.ctx.rom.bytes==first); /* allocator deliberately reuses storage */
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==3);
    seed_guard(3);
    struct gf5_rom_view owned=host.ctx.rom;
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_EBUSY);
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==4);
    assert(host.ctx.rom.bytes==owned.bytes &&
           host.ctx.rom.memory_program==owned.memory_program && host.live_allocs==2);
    gf5_rom_release(&host.ctx);
    gf5_host_trace_clear(&host);
    seed_guard(U32_MAX);
    gf5_rom_release(&host.ctx); /* empty release and generation wrap */
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==0);
    assert(!host.calls[F5_FREE]);
    seed_guard(U32_MAX);
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_OK);
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==0);
    gf5_rom_release(&host.ctx);

    fixture(); seed_guard(0);
    assert(gf5_rom_acquire(&host.ctx,0,sizeof(rom),&program)==GF5_EINVAL);
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==1);
    fixture(); seed_guard(0); rom[FIX_BIT_ENTRY + 2]=0;
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_EBIOS);
    assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==1);
    for (u32 nth=1; nth<=2; ++nth) {
        fixture(); seed_guard(0); host.observe=invalid_before_callback;
        gf5_host_fail_at(&host,F5_ALLOC,nth,GF5_ENOMEM);
        assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_ENOMEM);
        assert(!host.ctx.plan_guard.valid && host.ctx.rom_generation==1);
        assert(!host.live_allocs && !host.ctx.rom.bytes);
    }
    assert(gf5_rom_acquire(0,rom,sizeof(rom),&program)==GF5_EINVAL);
    gf5_rom_release(0);
    struct gf5_host_activity activity=gf5_host_activity(&host);
    assert(!activity.reads && !activity.writes && !activity.pauses && !activity.resumes);
}
void _start(void) { test(); rom_guard(); gf2_host_exit(0); }
