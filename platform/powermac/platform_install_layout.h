/* Installer implementation and APM state are supplied in the PowerMac stage. */
#ifndef PLATFORM_INSTALL_LAYOUT_H
#define PLATFORM_INSTALL_LAYOUT_H

#define PLATFORM_HAS_INSTALL_TARGET 0

typedef struct {
    int drive;
} HdprepGeom;

typedef struct {
    int drive;
    unsigned long start;
    unsigned long len;
} HdprepPlan;

#define PLATFORM_BOOT_COUNT 1
#define PLATFORM_BOOT_AFTER_FILES 1

#endif
