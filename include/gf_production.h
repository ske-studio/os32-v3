/* Core GF selection and boot-lifetime return hook. No KDEBUG dependency. */
#ifndef GF_PRODUCTION_H
#define GF_PRODUCTION_H
int gf_production_selected(void);
/* Called once before gfx_boot_reserve; GUI omitted is supplied as zero. */
void gf_production_configure(const char *gfx, int gui);
/* P5 pins the module/context before registration. Callback performs GPU stop,
 * route00, decode check, then (only if safe) 98 restoration/cells/redraw.
 * Keep it registered after stop: reservations and module live until reboot. */
void gf_production_set_return(int (*callback)(void *), void *opaque);
int gf_production_return(void);
#endif
