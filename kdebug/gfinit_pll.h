#ifndef KDEBUG_GFINIT_PLL_H
#define KDEBUG_GFINIT_PLL_H
#include "../include/types.h"
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
