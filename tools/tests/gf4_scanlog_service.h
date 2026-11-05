/* Only fixed-board devinit scripts are a seam (covered by runner hosts).
 * ROM acquisition, DCB/PLL parsing, baseline and F5 preparation stay real. */
int scanlog_devinit(struct gf_init_context *,struct gf_init_program *);
#define gf_init_run scanlog_devinit
#include "../../hwdev/gf/display/service.c"
#undef gf_init_run
