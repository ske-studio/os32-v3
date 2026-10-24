#ifndef KDEBUG_GFINIT_PLL_H
#define KDEBUG_GFINIT_PLL_H
#include "../include/types.h"
/* Version 0x21 PLL limit table: nouveau MIT 5b0764a53235,
 * nvkm/subdev/bios/pll.c:301-331. */
#define GF_PLL_TBL_VERSION 0x21
#define GF_PLL_TBL_HDR_MIN 4
#define GF_PLL_TBL_ENTRY_MIN 31
#define GF_PLL_TBL_COUNT 4
#define GF_PLL_HDR_VERSION 0
#define GF_PLL_HDR_HEADER_LEN 1
#define GF_PLL_HDR_ENTRY_LEN 2
#define GF_PLL_HDR_COUNT 3
#define GF_PLL_ENT_REG 0
#define GF_PLL_ENT_VCO1_MIN 4
#define GF_PLL_ENT_VCO1_MAX 6
#define GF_PLL_ENT_VCO2_MIN 8
#define GF_PLL_ENT_VCO2_MAX 10
#define GF_PLL_ENT_VCO1_INPUT_MIN 12
#define GF_PLL_ENT_VCO2_INPUT_MIN 14
#define GF_PLL_ENT_VCO1_INPUT_MAX 16
#define GF_PLL_ENT_VCO2_INPUT_MAX 18
#define GF_PLL_ENT_VCO1_N_MIN 20
#define GF_PLL_ENT_VCO1_N_MAX 21
#define GF_PLL_ENT_VCO1_M_MIN 22
#define GF_PLL_ENT_VCO1_M_MAX 23
#define GF_PLL_ENT_VCO2_N_MIN 24
#define GF_PLL_ENT_VCO2_N_MAX 25
#define GF_PLL_ENT_VCO2_M_MIN 26
#define GF_PLL_ENT_VCO2_M_MAX 27
#define GF_PLL_ENT_MAX_P 29
#define GF_PLL_ENT_BIAS_P 30
#define GF_PLL_ENT_REF 31
#define GF_PLL_ENT_REF_LAST (GF_PLL_ENT_REF + 3)
#define GF_PLL_KHZ_PER_MHZ 1000
#define GF_PLL_MARGIN_DIV 200
/* P/N/M and S2 enable: envytools MIT f102b82381f3,
 * rnndb/display/nv3_pramdac.xml:10-30
 * nv3_pll/nv31_pll_s2. Single-stage, memory bias and c040 masks:
 * nouveau MIT 5b0764a53235 nvkm/subdev/devinit/nv04.c:173-331,
 * setPLL_double_{high,low}regs.
 * rnndb/pm/nv40_pclock.xml does not describe the low-register high bits;
 * keep their unconfirmed literals in the sequence rather than invent names. */
#define GF_PLL_CTRL_OFFSET 4
#define GF_PLL_CTRL_P_SHIFT 16
#define GF_PLL_CTRL_P_MASK (7U << GF_PLL_CTRL_P_SHIFT)
#define GF_PLL_CTRL_P_MASK_MEM (0x77U << GF_PLL_CTRL_P_SHIFT)
#define GF_PLL_CTRL_BIAS_SHIFT 20
#define GF_PLL_CTRL_SINGLE_MEM (1U << 12)
#define GF_PLL_CTRL_SINGLE (1U << 8)
#define GF_PLL_VIDEO_S2_ENABLE 0x80000000U
#define GF_PLL_VIDEO_S2_KEEP 0x7fff0000U
#define GF_PLL_VIDEO_KEEP 0xfff80000U
#define GF_PLL_GATE_VIDEO (3U << 16)
#define GF_PLL_GATE_MEM (3U << 14)
#define GF_PLL_GATE_CORE 0x333U
#define GF_PLL_REF 27000U
#define GF_PLL_P_MAX 6U
#define GF_PLL_NM_MAX 255U
#define GF_PLL_CORE 0x4004U
#define GF_PLL_MEMORY 0x4024U
#define GF_PLL_VIDEO 0x680508U
#define GF_PLL_MEMORY_MIRROR 0x4038U
#define GF_PLL_MEMORY_COEFF 0x403cU
#define GF_PLL_GATE 0xc040U
#define GF_PLL_MEMORY_POWER 0x4600U
#define GF_PLL_VIDEO_COEFF 0x680578U
#define GF_PLL_VIDEO_STAGE 0x680580U
#define GF_PLL_WRITES_MAX 15U
#define GF_PLL_SUBSTEP 0x12000U
#define GF_PLL_SUBSTEP_STRIDE 32U /* core, memory, video */
struct gfinit_pll_vco { u32 min_freq, max_freq, min_input, max_input, min_n, max_n, min_m, max_m; };
struct gfinit_pll_limits { struct gfinit_pll_vco vco1, vco2; u32 max_p, bias_p, ref; };
struct gfinit_pll_solution { u32 n1, m1, n2, m2, p, actual, error; };
/* Each write is (captured source & keep) | value; source=0 means literal.
 * Captures belong only to this uninterrupted instruction, never cleanup. */
struct gfinit_pll_write { u32 reg, source, keep, value; };
struct gfinit_pll_sequence { u32 count; struct gfinit_pll_write writes[GF_PLL_WRITES_MAX]; };
int gfinit_pll_parse(const u8 *rom, u32 size, u32 table, u32 reg, struct gfinit_pll_limits *out);
int gfinit_pll_solve(const struct gfinit_pll_limits *limits, u32 khz, struct gfinit_pll_solution *out);
int gfinit_pll_sequence(u32 reg, const struct gfinit_pll_limits *limits,
                        const struct gfinit_pll_solution *s, struct gfinit_pll_sequence *out);
#endif
