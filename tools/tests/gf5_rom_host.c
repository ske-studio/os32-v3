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
void _start(void) { test(); gf2_host_exit(0); }
