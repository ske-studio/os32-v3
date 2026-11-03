/* Link hwdev/gf/grctx.c separately. Independent MIT oracle: nouveau
 * c03c1a1 nvkm/engine/gr/ctxnv40.c, included unchanged via GF2_B4_ORACLE.
 * A3 supplies -DGF2_B4_ORACLE='"<absolute path>/ctxnv40.c"' and an empty
 * core/gpuobj.h on the include path; the Linux object/services are stubbed
 * below. No oracle data is generated from the implementation under test. */
#include "../../hwdev/gf/command.h"

static void fail(int line)
{ __asm__ volatile("int $0x80" : : "a"(1), "b"(line)); for (;;) {} }
#define CHECK(x) do { if (!(x)) fail(__LINE__); } while (0)
#define GF2_HOST_B4
#include "gf_command_host.h"

/* Host adapters for the unmodified MIT oracle, including its own helpers
 * from ctxnv40.h. nv40.h's unrelated engine interfaces are not needed. */
struct nvkm_device { u32 chipset; };
struct nvkm_gpuobj { u32 *words; };
static void nvkm_wo32(struct nvkm_gpuobj *obj, u32 off, u32 value)
{
    CHECK(!(off & 3) && off < image_bytes);
    obj->words[off / 4] = value;
}
static void nvkm_wr32(struct nvkm_device *dev, u32 off, u32 value)
{
    (void)dev;
    if (off == 0x400324) { CHECK(value == 0); upload_words = 0; }
    else { CHECK(off == 0x400328 && upload_words < 256); uploaded[upload_words++] = value; }
}
#define __NV40_GR_H__
/* nv40.h:41-49, unchanged class selection for chipset 4a. */
#define nv44_gr_class(d) (((d)->chipset & 0xf0) == 0x60 || \
                         !(0x0aaf & (1 << ((d)->chipset & 0x0f))))
#define BUG_ON(x) CHECK(!(x))
#define GFP_KERNEL 0
#define ENOMEM 12
#define kmalloc(bytes, flags) oracle_code
#define kfree(ptr) ((void)(ptr))
/* The upstream MIT helpers mix int loop counters with u32 lengths. Keep
 * that source intact while checking OS32 code with -Wall -Wextra -Werror. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include GF2_B4_ORACLE
#pragma GCC diagnostic pop

void _start(void)
{
    struct nvkm_device device = { .chipset = 0x4a };
    struct nvkm_gpuobj obj = { .words = expected };
    struct gf_foundation f = {0};
    struct gf_inst inst = { .off = 0x40000 };
    struct nvkm_grctx vals = { .device = &device, .mode = NVKM_GRCTX_VALS, .data = &obj };
    u32 code[GF2_GRCTX_PROGRAM_WORDS + 1], words, bytes, good_writes;

    code[GF2_GRCTX_PROGRAM_WORDS] = 0x12345678;
    CHECK(gf2_grctx_program(code, &words, &bytes) == GF2_OK);
    CHECK(nv40_grctx_init(&device, &image_bytes) == 0);
    CHECK(words == upload_words && words > 0 && bytes == image_bytes);
    CHECK(bytes <= sizeof(actual) && !(bytes & 3));
    CHECK(code[GF2_GRCTX_PROGRAM_WORDS] == 0x12345678);
    for (u32 i = 0; i < words; i++) CHECK(code[i] == uploaded[i]);
    nv40_grctx_generate(&vals);
    CHECK(vals.ctxvals_pos * 4 == bytes);
    inst.size = bytes;
    CHECK(gf2_grctx_fill(&f, &inst) == GF2_OK);
    good_writes = writes;
    for (u32 i = 0; i < IMAGE_WORDS; i++) CHECK(actual[i] == expected[i]);
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
