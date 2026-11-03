/* Link with hwdev/gf/gr.c only. Dependency stubs below can move to A2;
 * their names/signatures are the command.h / foundation.h ABI. No B4 oracle
 * is duplicated here: B4 separately checks the real generator against MIT. */
#include "../../hwdev/gf/command.h"

static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
struct event { u32 write, off, value; };
static struct event events[256];
static u32 memory[16], count, cursor, fail_io, units, overlap, words = 3;
static u32 inst_calls, fail_inst;
static int alloc_rc, fill_rc, bind_rc, ramht_rc;
static bool live, published, bound;
static unsigned frees, adds;
static struct gf_foundation foundation;
static struct gf2_chan channel;

static int rd(void *p, u32 bar, u32 off, u32 width, u32 *value)
{
    (void)p;
    CHECK(bar == 0 && width == 4 && count < 256);
    *value = 0;
    if (off == 0x1540) *value = units;
    else if (off == 0x100200) *value = 0x12345678;
    else if (off == 0x100204) *value = 0x87654321;
    else {
        CHECK(off >= 0x100600 && off < 0x1006c0);
        u32 i = (off - 0x100600) / 16, part = off & 15;
        if (part == 0) *value = overlap && i == 11 ? channel.B | 1 : i * 0x1000 + 1;
        else if (part == 4) *value = overlap && i == 11 ? channel.B : i * 0x1000 + 0xfff;
        else { CHECK(part == 8); *value = (i + 1) * 0x100; }
    }
    events[count++] = (struct event){0, off, *value};
    return count == fail_io ? -1 : 0;
}
static int wr(void *p, u32 bar, u32 off, u32 width, u32 value)
{
    (void)p;
    CHECK(bar == 0 && width == 4 && count < 256);
    if (off == 0x400720) CHECK(bound);
    events[count++] = (struct event){1, off, value};
    return count == fail_io ? -1 : 0;
}
int gf2_grctx_program(u32 code[GF2_GRCTX_PROGRAM_WORDS], u32 *n, u32 *bytes)
{ code[0] = 0x123; code[1] = 0x456; code[2] = 0x789; *n = words; *bytes = 64; return 0; }
int gf2_grctx_fill(struct gf_foundation *f, const struct gf_inst *ctx)
{ CHECK(f == &foundation && live && ctx == &channel.grctx); memory[1] = 0xfeed; return fill_rc; }
int gf_inst_alloc(struct gf_foundation *f, u32 bytes, u32 align, bool zero, struct gf_inst *out)
{
    CHECK(f == &foundation && !live && align == 16 && bytes <= sizeof(memory));
    CHECK((bytes == 64 && zero) || (bytes == 20 && !zero));
    if (alloc_rc) return alloc_rc;
    *out = (struct gf_inst){0x40000, bytes, 0};
    f->imem.used = out; live = true;
    for (unsigned i = 0; i < 16; i++) memory[i] = zero ? 0 : 0xdead;
    return 0;
}
void gf_inst_free(struct gf_foundation *f, struct gf_inst *inst)
{ CHECK(live && f->imem.used == inst); f->imem.used = 0; live = false; frees++; }
int gf_inst_write32(struct gf_foundation *f, const struct gf_inst *inst, u32 off, u32 value)
{
    CHECK(live && f->imem.used == inst && !(off & 3) && off < inst->size);
    if (++inst_calls == fail_inst) return GF1_EIO;
    memory[off / 4] = value; published = false; return 0;
}
int gf_inst_read32(struct gf_foundation *f, const struct gf_inst *inst, u32 off, u32 *value)
{
    CHECK(live && f->imem.used == inst && off == 0);
    if (++inst_calls == fail_inst) return GF1_EIO;
    *value = memory[0]; published = true; return 0;
}
int gf2_fifo_bind_gr(struct gf2_chan *ch, u32 off)
{
    CHECK(ch == &channel && published && off == ch->grctx.off);
    CHECK(memory[0] == off >> 4 && memory[1] == 0xfeed);
    bound = !bind_rc; return bind_rc;
}
int gf2_ramht_add(struct gf2_chan *ch, u32 handle, u8 engine, u32 off, u32 *cookie)
{
    CHECK(ch == &channel && handle == GF2_HANDLE_LAYER3_FIRST);
    CHECK(engine == 1 && off == 0x40000 && published);
    for (unsigned i = 1; i < 5; i++) CHECK(memory[i] == 0);
    adds++; if (!ramht_rc) *cookie = 9; return ramht_rc;
}
static void reset(void)
{
    foundation = (struct gf_foundation){0}; channel = (struct gf2_chan){0};
    foundation.io.read = rd; foundation.io.write = wr; channel.f = &foundation;
    foundation.imem.vram_bytes = 0x4000000; channel.bar1.length = 0x8000000;
    channel.B = 0x3000000; units = 0x108; overlap = 0; words = 3;
    count = cursor = fail_io = frees = adds = 0;
    inst_calls = fail_inst = 0;
    alloc_rc = fill_rc = bind_rc = ramht_rc = 0; live = published = bound = false;
}
static void event(u32 write, u32 off, u32 value)
{
    CHECK(cursor < count && events[cursor].write == write);
    CHECK(events[cursor].off == off && events[cursor].value == value); cursor++;
}
void _start(void)
{
    static const u32 offsets[] = {
        0x400080,0x400080,0x400084,0x40008c,0x400090,0x400610,0x400144,0x400714,
        0x400820,0x400824,0x400860,0x400864,0x400b38,0x400b3c,0x400bc4,0x400bbc
    }, values[] = {
        0xffffffff,0,0x401287c0,0xe0de8055,0x8000,0xbe3c5f,0x10010100,0xffffffff,
        0x83280eff,0xa0,0,0,0x2ffff800,0x6000,0x1003d888,0xb7a7b500
    };
    static const u16 classes[] = {
        0x12,0x19,0x30,0x39,0x43,0x44,0x4a,0x62,0x72,0x89,0x8a,0x9f,0x3062,0x3089,0x309e,0x4497
    };
    reset(); CHECK(gf2_gr_init(&channel) == 0 && live && !frees);
    event(1,0x400324,0); event(1,0x400328,0x123); event(1,0x400328,0x456);
    event(1,0x400328,0x789); event(1,0x40032c,0);
    for (unsigned i = 0; i < 8; i++) event(1,offsets[i],values[i]);
    event(0,0x1540,0x108); event(1,0x405000,3);
    for (unsigned i = 8; i < 16; i++) event(1,offsets[i],values[i]);
    for (unsigned i = 0; i < 4; i++) {
        u32 value = i & 1 ? 0x87654321 : 0x12345678;
        event(0,i & 1 ? 0x100204 : 0x100200,value);
        event(1,(i < 2 ? 0x4009f0 : 0x4069f0) + (i & 1) * 4,value);
    }
    event(1,0x400840,0); event(1,0x400844,0);
    event(1,0x4008a0,0x7ffffff); event(1,0x4008a4,0x7ffffff);
    for (unsigned i = 0; i < 12; i++) {
        event(0,0x100600+i*16,i*0x1000+1); event(0,0x100604+i*16,i*0x1000+0xfff);
        event(0,0x100608+i*16,(i+1)*0x100);
    }
    for (unsigned i = 0; i < 12; i++) {
        event(1,0x400908+i*16,(i+1)*0x100); event(1,0x400904+i*16,i*0x1000+0xfff);
        event(1,0x400900+i*16,i*0x1000+1);
    }
    event(1,0x400720,1); CHECK(cursor == count);
    u32 total = count;
    for (u32 i = 1; i <= total; i++) {
        reset(); fail_io = i;
        CHECK(gf2_gr_init(&channel) == GF2_EIO && count == i && !live);
    }
    reset(); words = 257; CHECK(gf2_gr_init(&channel) == GF2_EIO && !count);
    reset(); units = 0; CHECK(gf2_gr_init(&channel) == 0 && count == total-1);
    reset(); overlap = 1; CHECK(gf2_gr_init(&channel) == GF2_ESTATE && !live);
    reset(); alloc_rc = GF1_ENOSPC; CHECK(gf2_gr_init(&channel) == GF2_ENOSPC && !frees);
    reset(); fill_rc = GF2_EIO; CHECK(gf2_gr_init(&channel) == GF2_EIO && frees == 1);
    reset(); bind_rc = GF2_EIO; CHECK(gf2_gr_init(&channel) == GF2_EIO && frees == 1);
    for (u32 i = 1; i <= 2; i++) {
        reset(); fail_inst = i;
        CHECK(gf2_gr_init(&channel) == GF2_EIO && frees == 1 && !bound);
    }
    for (unsigned i = 0; i < sizeof(classes)/sizeof(classes[0]); i++) {
        struct gf2_object obj = {0}; reset();
        CHECK(gf2_gr_object_new(&channel,GF2_HANDLE_LAYER3_FIRST,classes[i],&obj) == 0);
        CHECK(obj.cookie == 9 && obj.class_id == classes[i] && memory[0] == classes[i]);
        CHECK(obj.handle == GF2_HANDLE_LAYER3_FIRST && obj.inst.size == 20 && adds == 1);
    }
    struct gf2_object obj = {0}; reset();
    CHECK(gf2_gr_object_new(&channel,GF2_HANDLE_LAYER3_FIRST,0xffff,&obj) == GF2_ECLASS && !live);
    reset(); ramht_rc = GF2_EEXIST;
    CHECK(gf2_gr_object_new(&channel,GF2_HANDLE_LAYER3_FIRST,0x39,&obj) == GF2_EEXIST);
    CHECK(frees == 1 && !live && !obj.inst.size);
    for (u32 i = 1; i <= 6; i++) {
        reset(); fail_inst = i;
        CHECK(gf2_gr_object_new(&channel,GF2_HANDLE_LAYER3_FIRST,0x39,&obj) == GF2_EIO);
        CHECK(frees == 1 && !live && !adds);
    }
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
