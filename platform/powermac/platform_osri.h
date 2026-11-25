#ifndef OS32_PLATFORM_OSRI_H
#define OS32_PLATFORM_OSRI_H

/* The resident /host mount can coexist with OF display. This image has no PCM. */
static inline int platform_osri_mount_busy(void) { return 0; }
static inline int platform_osri_pcm_busy(void) { return 0; }

#endif
