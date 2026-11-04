#ifndef GFX_OSRI_H
#define GFX_OSRI_H

#include "types.h"
#include "gfx.h"
#include "osri_profile.h"

#define OSRI_HEADER_BYTES 64u
#define OSRI_PALETTE_OFF 64u
#define OSRI_PIXELS_OFF 96u
#define OSRI_PIXELS_BYTES 128000u
#define OSRI_EVENTS_OFF 128096u
#define OSRI_EVENT_BYTES 8u
#define OSRI_VERSION_1 1u
#define OSRI_VERSION_2 2u
#define OSRI_V1_MAX_EVENTS 200u
#define OSRI_COLORS 16u
/* File limits include same-color events, independently of OUT cost. */
#define OSRI_V2_MAX_EVENTS_PER_LINE OSRI_COLORS
#define OSRI_V2_MAX_EVENTS ((GFX_HEIGHT - 1u) * OSRI_V2_MAX_EVENTS_PER_LINE)
#define OSRI_BUDGET_ROW_EVENTS ((OSRI_DEFAULT_OUT_BUDGET + 1u) / 2u)
#define OSRI_IMAGE_ROW_EVENTS \
    ((OSRI_BUDGET_ROW_EVENTS < OSRI_COLORS) ? OSRI_BUDGET_ROW_EVENTS : OSRI_COLORS)
#define OSRI_IMAGE_V2_EVENTS ((GFX_HEIGHT - 1u) * OSRI_IMAGE_ROW_EVENTS)
#define OSRI_IMAGE_EVENTS \
    ((OSRI_IMAGE_V2_EVENTS > OSRI_V1_MAX_EVENTS) ? OSRI_IMAGE_V2_EVENTS : OSRI_V1_MAX_EVENTS)
#define OSRI_MAX_EVENTS OSRI_V1_MAX_EVENTS

/* Layout describes the file; decode little endian fields rather than casting. */
struct OsriHeader {
    u8 magic[4];
    u16 version, header_bytes, width, height;
    u8 bpp, color_format;
    u16 flags;
    u32 file_bytes, palette_off, pixels_off, pixels_bytes, events_off, event_count;
    u16 event_bytes, reserved_profile, band_lines, reserved0;
    u32 file_crc32, reserved[3];
};
struct OsriEvent { u16 line; u8 index, reserved0; u16 rgb444, reserved1; };
struct OsriImage {
    u16 pal[OSRI_COLORS], count, band, version;
    struct OsriEvent event[OSRI_IMAGE_EVENTS];
};
STATIC_ASSERT(OSRI_DEFAULT_OUT_BUDGET == 31u, osri_default_budget_b31);
STATIC_ASSERT(OSRI_IMAGE_EVENTS == 6384u, osri_image_events);
STATIC_ASSERT(OSRI_IMAGE_EVENTS <= 65535u, osri_count_width);
STATIC_ASSERT(sizeof(((struct OsriImage *)0)->count) == 2u, osri_count_size);
STATIC_ASSERT(sizeof(struct OsriImage) == 51110u, osri_image_size);
STATIC_ASSERT(sizeof(struct OsriHeader) == OSRI_HEADER_BYTES, osri_header_size);
STATIC_ASSERT(__builtin_offsetof(struct OsriHeader, file_crc32) == 48, osri_crc_offset);
STATIC_ASSERT(sizeof(struct OsriEvent) == OSRI_EVENT_BYTES, osri_event_size);

/* Plane capacity is guaranteed by gfx_mode_fits' ledger check at mode selection. */
/* Returns 0 on success, -1 on failure. */
int osri_load(const char *path, struct gfx_kernel_fb *dst, struct OsriImage *image);
#endif
