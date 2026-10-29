/* Shared NV44A analog load order. nouveau c03c1a1 MIT:
 * dispnv04/crtc.c:708 prepare, :731 commit; hw.c:827 load_state.
 * Per-register file:line references remain in tools/gfinit_mode_tables.py.
 * No REC, diagnostic VRAM, PC-98 routing or direct hardware executor here. */
#include "gf_load_state.h"
#include "generated/gf_mode_rows.inc"
/* Existing stage-1 row boundaries, not ROM offsets. */
enum gf_load_boundary { GF_LOAD_ROW_P=2, GF_LOAD_ROW_SD=15,
    GF_LOAD_ROW_L1=26, GF_LOAD_ROW_L2=46, GF_LOAD_ROW_L3=120,
    GF_LOAD_ROW_COMMIT=190, GF_LOAD_DPMS_OFF=5, GF_LOAD_DPMS_ON=200 };
int gf_load_state(const struct gfinit_mode *mode, void *saved, const struct gf_load_io *io)
{
    if (io->section(saved, GF_LOAD_Q)) return -1;
    for (u32 i=0; i<gf_mode_row_count; i++) {
        u32 section;
        switch (i) {
        case GF_LOAD_ROW_P: section=GF_LOAD_P; break;
        case GF_LOAD_ROW_SD: section=GF_LOAD_SD; break;
        case GF_LOAD_ROW_L1: section=GF_LOAD_L1; break;
        case GF_LOAD_ROW_L2: section=GF_LOAD_L2; break;
        case GF_LOAD_ROW_L3: section=GF_LOAD_L3; break;
        case GF_LOAD_ROW_COMMIT: section=GF_LOAD_COMMIT; break;
        default: section=GF_LOAD_Q; break;
        }
        if (section!=GF_LOAD_Q && io->section(saved,section)) return -1;
        if (io->row(saved,mode,i)) return -1;
        if ((gf_mode_rows[i].flags & GF_LOAD_FP_ON) &&
            io->wait(saved,GF_LOAD_FP_WAIT_MS,true)) return -1;
        if ((i==GF_LOAD_DPMS_OFF || i==GF_LOAD_DPMS_ON) &&
            io->wait(saved,GF_LOAD_DPMS_WAIT_MS,false)) return -1;
    }
    return 0;
}
