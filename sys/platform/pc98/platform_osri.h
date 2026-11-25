#ifndef OS32_PLATFORM_OSRI_H
#define OS32_PLATFORM_OSRI_H

#include "pcm_contract.h"
#include "../../fs/serialfs/serialfs.h"

#define platform_osri_mount_busy() serialfs_is_mounted()
#define platform_osri_pcm_busy() (pcm_state() != PCM_ST_CLOSED)

#endif
