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

/* Media names are supplied with the PowerMac installer. */
extern const char SRC_BOOT_HDD[], SRC_LOADER_H[];
extern const char BOOT_IPL_NAME[], BOOT_LOADER_NAME[];

#endif
