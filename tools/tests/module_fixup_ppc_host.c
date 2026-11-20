/* KMOD PPC wire example: base 1 + VA 0x7fff has HA=1 and LO=0x8000. */
#include "module_image.h"
#include "module_fixup_ppc.inc"

static int fixture(void)
{
    u8 rec[] = {2,0,0,0, 6,0,0,0, 255,127,0,0,
                6,0,0,0, 4,0,0,0, 255,127,0,0};
    u8 body[] = {0x3c,0x80,0,0, 0x38,0x84,0,0};
    if (module_fixup_ppc_validate(rec, 2, 0x8000, 8)) return 1;
    module_fixup_ppc_apply(body, 1, rec, 2);
    if (body[0]!=0x3c || body[1]!=0x80 || body[2]!=0 || body[3]!=1 ||
        body[4]!=0x38 || body[5]!=0x84 || body[6]!=0x80 || body[7]!=0) return 2;
    rec[12] = 2;
    if (module_fixup_ppc_validate(rec, 2, 0x8000, 8) != -MODULE_RELOC_INVALID) return 3;
    rec[12] = 0;
    rec[13] = 0x80;
    if (module_fixup_ppc_validate(rec, 2, 0x8000, 8) != -MODULE_RELOC_INVALID) return 4;
    rec[13] = 0;
    rec[12] = 5;
    if (module_fixup_ppc_validate(rec, 2, 0x8000, 8) != -MODULE_RELOC_INVALID) return 5;
    return 0;
}

void _start(void)
{
    int rc = fixture();
    __asm__ volatile("int $0x80" :: "a"(1), "b"(rc));
    __builtin_unreachable();
}
