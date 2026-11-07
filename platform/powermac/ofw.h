#ifndef OS32_POWERMAC_OFW_H
#define OS32_POWERMAC_OFW_H
#include "ppc_boot.h"
#include "ppc_layout.h"
struct ofw_env {
    int in, out, memory, mmu, real_mode;
    unsigned ac, sc;
};
void ofw_init(ppc_ci ci);
/* 0 success, -1 console unavailable, -2 invalid environment. */
int ofw_setup(struct ofw_env *e);
int ofw_option_truefalse(const char *prop, int proplen);
/* cells: ni inputs followed by no outputs; returns firmware CI status. */
int ofw_call(const char *name, unsigned ni, unsigned no, cell *cells);
int ofw_finddevice(const char *path);
int ofw_getprop(int ph, const char *name, void *buf, unsigned cap);
int ofw_child(int ph);
int ofw_peer(int ph);
int ofw_parent(int ph);
int ofw_package_to_path(int ph, char *buf, unsigned cap);
int ofw_open(const char *path);
int ofw_write(int ih, const void *buf, unsigned n);
void ofw_exit(void);
/* Only method arguments/results are reversed. catch != 0 is failure. */
int ofw_call_method(const char *method, int ih, unsigned ni,
                    unsigned no, cell *cells);
int ofw_map_io(int mmu, unsigned ac, cell pa);
struct pm_escc { cell csr, data; int ih; };
/* -1 tree/property/address, -2 open, -3 map; handle retained until stop. */
int pm_escc_prepare(const struct ofw_env *e, struct pm_escc *out);
int pm_escc_write(const struct pm_escc *s, const char *p, unsigned n);
int pm_escc_drain(const struct pm_escc *s);
#endif
