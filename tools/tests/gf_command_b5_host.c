/* Link hwdev/gf/gr.c only; A2 supplies B0/B3/B4 and foundation stubs.
 * B4 separately checks the real generator against the independent MIT oracle. */
#include "../../hwdev/gf/command.h"

static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
#define GF2_HOST_B5
#include "gf_command_host.h"

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
