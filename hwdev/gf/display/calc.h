/* Pure display state components; shared by production and diagnostic C. */
#ifndef GF_DISPLAY_CALC_H
#define GF_DISPLAY_CALC_H
#include "../../gfinit_mode.h"
static inline u32 mode_bit(u32 value, u32 from, u32 to)
{ return ((value >> from) & 1U) << to; }
void gf_mode_timing(struct gfinit_mode *o);
void gf_mode_format(struct gfinit_mode *o);
void gf_mode_pitch(struct gfinit_mode *o);
void gf_mode_start(struct gfinit_mode *o);
void gf_mode_dac_format(struct gfinit_mode *o);
void gf_mode_output(struct gfinit_mode *o);
void gf_mode_cursor(struct gfinit_mode *o);
void gf_mode_fifo(struct gfinit_mode *o);
void gf_mode_extended(const struct gfinit_mode_saved *saved, struct gfinit_mode *o);
int gf_mode_pll_program(const struct gfinit_pll_limits *, const struct gfinit_mode_saved *, struct gfinit_mode *);
#endif
