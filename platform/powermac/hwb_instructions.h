#ifndef PM_HWB_INSTRUCTIONS_H
#define PM_HWB_INSTRUCTIONS_H

struct transfer {
    int (*run)(u32, u32, u32, u32);
    const char *pc, *resume;
};
#define DECLARE(name) \
    extern int ppc_hwb_##name(u32, u32, u32, u32); \
    extern char ppc_hwb_##name##_insn[], ppc_hwb_##name##_resume[]
DECLARE(read8);
DECLARE(read16);
DECLARE(read16le);
DECLARE(read32);
DECLARE(read32le);
DECLARE(write8);
DECLARE(write16);
DECLARE(write16le);
DECLARE(write32);
DECLARE(write32le);
#define ENTRY(name) {ppc_hwb_##name, ppc_hwb_##name##_insn, ppc_hwb_##name##_resume}
static const struct transfer transfers[2][3][2] = {
    {{ENTRY(read8), ENTRY(read8)}, {ENTRY(read16), ENTRY(read16le)},
     {ENTRY(read32), ENTRY(read32le)}},
    {{ENTRY(write8), ENTRY(write8)}, {ENTRY(write16), ENTRY(write16le)},
     {ENTRY(write32), ENTRY(write32le)}}
};
#undef DECLARE
#undef ENTRY
#endif
