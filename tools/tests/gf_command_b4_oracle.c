/* Local-only emitter for the unmodified MIT nouveau oracle. This executable
 * neither includes nor links the OS32 grctx implementation or frozen data. */
typedef unsigned int u32;
#include "gf_command_runtime_host.h"
#define CHECK(x) assert(x)

enum { IMAGE_WORDS = 80 * 1024 / 4, PROGRAM_WORDS = 256, META_WORDS = 4 };
static u32 wire[META_WORDS + PROGRAM_WORDS + IMAGE_WORDS];
static u32 oracle_code[PROGRAM_WORDS], upload_words, image_bytes;
static u32 *const uploaded = wire + META_WORDS;
static u32 *const expected = wire + META_WORDS + PROGRAM_WORDS;

struct nvkm_device { u32 chipset; };
struct nvkm_gpuobj { u32 *words; };
static void nvkm_wo32(struct nvkm_gpuobj *obj, u32 off, u32 value)
{
    CHECK(!(off & 3) && off < image_bytes && off < IMAGE_WORDS * 4);
    obj->words[off / 4] = value;
}
static void nvkm_wr32(struct nvkm_device *dev, u32 off, u32 value)
{
    (void)dev;
    if (off == 0x400324) { CHECK(value == 0); upload_words = 0; }
    else {
        CHECK(off == 0x400328 && upload_words < PROGRAM_WORDS);
        uploaded[upload_words++] = value;
    }
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#include GF2_B4_ORACLE
#pragma GCC diagnostic pop

void _start(void)
{
    struct nvkm_device device = { .chipset = 0x4a };
    struct nvkm_gpuobj obj = { .words = expected };
    struct nvkm_grctx vals = {
        .device = &device, .mode = NVKM_GRCTX_VALS, .data = &obj
    };
    CHECK(nv40_grctx_init(&device, &image_bytes) == 0);
    CHECK(image_bytes <= IMAGE_WORDS * 4 && !(image_bytes & 3));
    nv40_grctx_generate(&vals);
    wire[0] = upload_words;
    wire[1] = image_bytes;
    wire[2] = image_bytes / 4; /* final PROG ctxvals_pos, returned by init */
    wire[3] = vals.ctxvals_pos;
    const char *data = (const char *)wire;
    u32 left = sizeof(wire);
    while (left) {
        int count;
        __asm__ volatile("int $0x80" : "=a"(count)
                         : "0"(4), "b"(1), "c"(data), "d"(left)
                         : "memory", "cc");
        CHECK(count > 0 && (u32)count <= left);
        data += count;
        left -= count;
    }
    gf2_host_exit(0);
}
