#ifndef POWERMAC_MEMORY_H
#define POWERMAC_MEMORY_H
struct pm_kboot_info;
/* D3 must reject false before detect/paging. Input is B2's successful result. */
int pm_memory_validate(const struct pm_kboot_info *info);
#endif
