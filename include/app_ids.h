#ifndef OS32_APP_IDS_H
#define OS32_APP_IDS_H

/* Application/owner ID pool (D3). exec/appslot.h owns the pool; this header
 * only carries the numbers so core code (kernel/io_policy.c) can size
 * per-owner tables without pulling exec types into every image. */
/* 1 = シェル帯 (CUI シェル / gshell)。kernel/gui.h の GUI_SHELL_OWNER と同値。
 * 一致は exec/appslot.c の STATIC_ASSERT が固定する。 */
#define APP_ID_SHELL     1
#define APP_MAX_APPS     4                       /* 同時に生きる非シェル ID */
#define APP_ID_MIN       2
#define APP_ID_MAX       (APP_ID_MIN + APP_MAX_APPS - 1)   /* 5 */
#define APP_SLOT_COUNT   (APP_ID_MAX + 1)        /* 添字 = ID。0 は使わない */

#endif
