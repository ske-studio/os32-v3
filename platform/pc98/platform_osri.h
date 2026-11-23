#ifndef OS32_PLATFORM_OSRI_H
#define OS32_PLATFORM_OSRI_H

#include "pcm_contract.h"
#include "../../fs/serialfs/serialfs.h"

static inline int platform_osri_mount_busy(void) { return serialfs_is_mounted(); }
static inline int platform_osri_pcm_busy(void) { return pcm_state() != PCM_ST_CLOSED; }

#endif
