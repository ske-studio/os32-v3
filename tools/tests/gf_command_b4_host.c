/* Link hwdev/gf/grctx.c separately; expectations come from the pinned
 * independent MIT oracle. See gf_command_b4_fixture.h for regeneration. */
#include "../../hwdev/gf/command.h"

static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
#define GF2_HOST_B4
#include "gf_command_host.h"

#include "gf_command_b4_fixture.h"

void _start(void)
{
    struct gf_foundation f = {0};
    struct gf_inst inst = { .off = 0x40000 };
    u32 code[GF2_GRCTX_PROGRAM_WORDS + 1], words, bytes, good_writes;

    code[GF2_GRCTX_PROGRAM_WORDS] = 0x12345678;
    CHECK(gf2_grctx_program(code, &words, &bytes) == GF2_OK);
    CHECK(words == B4_PROGRAM_WORDS && bytes == B4_IMAGE_BYTES);
    CHECK(bytes == B4_PROG_END_WORDS * 4 && bytes == B4_VALS_END_WORDS * 4);
    CHECK(sizeof(actual) == sizeof(b4_image));
    CHECK(bytes <= sizeof(actual) && !(bytes & 3));
    CHECK(code[GF2_GRCTX_PROGRAM_WORDS] == 0x12345678);
    for (u32 i = 0; i < words; i++) CHECK(code[i] == b4_program[i]);
    inst.size = bytes;
    CHECK(gf2_grctx_fill(&f, &inst) == GF2_OK);
    good_writes = writes;
    for (u32 i = 0; i < IMAGE_WORDS; i++) CHECK(actual[i] == b4_image[i]);
    /* B5 alone owns ctx[0]; a fill must also preserve a zero-default slot. */
    actual[0] = 0xabcdef01; actual[1] = 0xabcdef02;
    CHECK(gf2_grctx_fill(&f, &inst) == GF2_OK);
    CHECK(actual[0] == 0xabcdef01 && actual[1] == 0xabcdef02);
    writes = 0; fail_write = 1;
    CHECK(gf2_grctx_fill(&f, &inst) == GF2_EIO && writes == fail_write);
    writes = 0; fail_write = good_writes;
    CHECK(gf2_grctx_fill(&f, &inst) == GF2_EIO && writes == fail_write);
    __asm__ volatile("int $0x80" : : "a"(1), "b"(0)); for (;;) {}
}
