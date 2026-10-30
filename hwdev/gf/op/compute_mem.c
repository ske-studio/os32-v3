/* NV44A init DATA runner. docs/design/hwdev/gf/op/compute_mem.md.
 * Semantics checked against MIT nouveau c03c1a1 nvkm/subdev/bios/init.c:1459;
 * NV44A devinit has no meminit callback, so this instruction is a no-op. */
#include "../internal.h"

int gf_op_compute_mem_run(struct gf_init_context *ctx,
                          const struct gfinit_instruction *insn)
{
    (void)ctx;
    (void)insn;
    return 0;
}
