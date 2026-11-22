#ifndef OS32_FEP_BOOT_H
#define OS32_FEP_BOOT_H

#include "module/module_runtime.h"

/* Called once after static SQL and the machine's HDD-root prerequisites. */
int fep_boot_static(void);
void fep_boot_user_path_set(const char *path);
void fep_boot_user_rc_set(int rc);

/* Resident transaction shared with the module-backed SQL boot path. */
int fep_boot_start(const struct module_handle *sqlite, int no_dictionary, int *budget);
int fep_boot_rollback(const struct module_handle *sqlite, int failure);
int fep_module_load_after_sql(const struct module_handle *sqlite);
void fep_module_publish(void);
#endif
