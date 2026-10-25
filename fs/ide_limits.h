/* FS-local selector mask using the IDE driver bounds. */
#ifndef FS_IDE_LIMITS_H
#define FS_IDE_LIMITS_H
#include "ide.h"
#define FS_IDE_DRIVE_ID_MASK (IDE_MAX_DRIVES - 1)
#endif
