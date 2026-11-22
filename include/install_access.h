#ifndef OS32_INSTALL_ACCESS_H
#define OS32_INSTALL_ACCESS_H

#include "platform_install_management.h"

/* Pin the unit across an accepted synchronous operation. Deferred operations
 * retain this record until completion, including across worker context switches. */
enum install_op { INSTALL_READ, INSTALL_WRITE, INSTALL_MOUNT, INSTALL_VIEW_READ, INSTALL_VIEW_WRITE, INSTALL_CHS, INSTALL_FORMAT };
struct install_access {
    u32 unit, generation, lba, count, epoch, phase;
    enum install_op op;
};
int install_admit(struct install_access *a, u32 unit, enum install_op op,
                  u32 lba, u32 count, const void *bytes);
int install_raw_io(struct install_access *a, u32 unit, u32 lba, u32 count,
                   void *dst, const void *src, int writing, int *handled);
int install_transfer_allowed(const struct install_access *a, u32 lba, u32 count);
void install_complete(struct install_access *a);
int install_format_begin(struct install_access *a, u32 unit, u32 start, u32 length);
int install_format_transfer(const struct install_access *a, u32 physical, u32 count,
                            void *dst, const void *src);
int install_format_finish(struct install_access *a, int rc);
void install_retry_recovery(void);
int install_mount_visible(u32 unit);
int install_view_io(struct install_access *a, u32 unit, u32 generation,
                    u32 physical, u32 count, void *dst, const void *src, int *handled);
int install_mount_lock(const struct install_access *a, unsigned *flags);
void install_mount_unlock(unsigned flags);
int install_sync_pin(struct _Device *dev, u32 start, struct install_access *pin);
void install_dirty_begin(void *ctx, struct _Device *dev, u32 start, u32 sectors);
void install_dirty_finish(void *ctx);
int install_dirty_transfer(void *ctx, struct _Device *dev, u32 lba, u32 count,
                           void *dst, const void *src, int *handled);
#endif
