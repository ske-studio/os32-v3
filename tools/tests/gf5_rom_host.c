#define GF5_HOST_UNIT 8
#include "gf5_fake.h"
#include "../../hwdev/gf/internal.h"

static struct gf5_host host;
static struct gf4_state state;
static struct gf5_host_event events[32];
static u8 arena[sizeof(struct gf_init_program) + 1024] __attribute__((aligned(16)));
static u8 rom[256];
static struct gf_init_program program;

static void fixture(void)
{
    gf5_host_init(&host, &state);
    host.events = events; host.cevents = 32;
    gf5_host_arena(&host, arena, sizeof(arena));
    gf2_host_memset(rom, 0, sizeof(rom));
    /* BIT header at 16, one six-byte M entry, payload at 80. */
    rom[16]=0xff; rom[17]=0xb8; rom[18]='B'; rom[19]='I'; rom[20]='T';
    rom[24]=12; rom[25]=6; rom[26]=1;
    rom[28]='M'; rom[29]=1; rom[30]=2; rom[32]=80;
    rom[80]=0xb4; rom[81]=0; rom[180]=0x71;
    gf2_host_memset(&program, 0, sizeof(program));
    program.count=1; program.insns[0].pc=180; program.insns[0].opcode=0x71;
}
static void test(void)
{
    fixture();
    assert(gf5_rom_acquire(&host.ctx, rom, sizeof(rom), &program)==GF5_OK);
    assert(host.live_allocs==2 && host.ctx.rom.size==sizeof(rom));
    assert(host.ctx.rom.bytes!=rom && host.ctx.rom.memory_program!=&program);
    assert(host.ctx.rom.memory_pc==180 && host.ctx.rom.bytes[180]==0x71);
    gf2_host_memset(rom, 0, sizeof(rom));
    gf2_host_memset(&program, 0, sizeof(program));
    assert(host.ctx.rom.bytes[180]==0x71 && host.ctx.rom.memory_program->count==1);
    assert(host.ctx.rom.memory_program->insns[0].pc==180);
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
    fixture(); rom[28]='X';
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),0)==GF5_OK);
    assert(host.live_allocs==1 && !host.ctx.rom.memory_program && !host.ctx.rom.memory_pc);
    gf5_rom_release(&host.ctx);
    fixture(); rom[30]=1;
    assert(gf5_rom_acquire(&host.ctx,rom,sizeof(rom),&program)==GF5_EBIOS);
    assert(!host.calls[F5_ALLOC] && !host.live_allocs);
}
void _start(void) { test(); gf2_host_exit(0); }
