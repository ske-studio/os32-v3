/* Observation image owner: one borrowed BB and one image/plan at a time. */
#ifdef SPLASH_RASTER_OBS
#include "platform_splash.h"
#include "gfx_osri.h"
#include "splash_raster_image.h"
#include "kmalloc.h"
#include "palette.h"
#include "serial_diag.h"
#include "serial.h"
#include "config.h"
#include "tvram.h"
#include "vfs.h"
#include "../lib/le_internal.h"

#define SPLASH_OBS_SECONDS 5u
#define SPLASH_OBS_US_PER_SECOND 1000000u
#define SPLASH_OBS_PATH "/boot/obs/N00.osri"
#define SPLASH_OBS_DIGITS (sizeof("/boot/obs/N") - 1u)
#define SPLASH_OBS_LABEL_ATTR TATTR_WHITE
#define SPLASH_OBS_CRC_OFFSET 48u
#define SPLASH_OBS_CRC_DIGITS 8u

static u32 obs_rounds(const struct SplashRaster *s)
{
    if (!s->checked || !s->dynamic_ok || !s->line_num_us || !s->line_den ||
        s->total_lines <= SPLASH_RASTER_VISIBLE_LINES) return 0;
    unsigned long long period = (unsigned long long)s->total_lines * s->line_num_us;
    unsigned long long duration = (unsigned long long)SPLASH_OBS_SECONDS *
                                 SPLASH_OBS_US_PER_SECOND * s->line_den;
    return (u32)((duration + period - 1) / period);
}

static void obs_black(void)
{
    for (u32 i = 0; i < OSRI_COLORS; i++) palette_set(i, 0, 0, 0);
}

/* Read the file's declared CRC for the start line. osri_load independently
 * validates the entire file before it can reach prepare/VRAM. */
static u32 obs_file_crc(const char *path)
{
    u8 header[OSRI_HEADER_BYTES];
    u32 pos = 0;
    int fd = vfs_open(path, O_RDONLY);
    if (fd < 0) return 0;
    while (pos < sizeof(header)) {
        int got = vfs_read_fd(fd, header + pos, sizeof(header) - pos);
        if (got <= 0 || (u32)got > sizeof(header) - pos) break;
        pos += (u32)got;
    }
    vfs_close(fd);
    return pos == sizeof(header) ? le_read32(header + SPLASH_OBS_CRC_OFFSET) : 0;
}

static void obs_start(u32 n, u32 crc)
{
    serial_puts_polled("SPLO n="); serial_diag_u32(n);
    serial_puts_polled(" start crc="); serial_diag_hex(crc, SPLASH_OBS_CRC_DIGITS);
    serial_puts_polled(" colors="); serial_diag_u32(n);
    serial_puts_polled(" houts="); serial_diag_u32(PL_OBS_COLOR_OUTS * n - 1);
    serial_puts_polled("\n");
}

static void obs_end(u32 n, const char *status, u32 frames, u32 drops)
{
    serial_puts_polled("SPLO n="); serial_diag_u32(n);
    serial_puts_polled(" end status="); serial_puts_polled(status);
    serial_puts_polled(" frames="); serial_diag_u32(frames);
    serial_puts_polled(" drop="); serial_diag_u32(drops);
    serial_puts_polled("\n");
}

void platform_splash_observe(void)
{
    /* The DIAG probe used to own this early initialization. OBS omits
     * that probe, but still emits SPLO before the shell initializes UART. */
    serial_init(SYS_SERIAL_BAUD);
    struct SplashRaster *s = pc98_splash_raster_state();
    u32 rounds = obs_rounds(s);
    for (u32 n = 1; n <= OSRI_COLORS; n++) {
        char path[] = SPLASH_OBS_PATH;
        struct gfx_kernel_fb fb;
        struct OsriImage *image = NULL;
        struct PlObsPlan *plan = NULL;
        OS32_Stat stat;
        u32 frames = 0, drops = 0;
        const char *status = "missing";
        path[SPLASH_OBS_DIGITS] = '0' + n / 10;
        path[SPLASH_OBS_DIGITS + 1] = '0' + n % 10;
        tvram_putchar_at(0, 0, 'N', SPLASH_OBS_LABEL_ATTR);
        tvram_putchar_at(1, 0, '=', SPLASH_OBS_LABEL_ATTR);
        tvram_putchar_at(2, 0, '0' + n / 10, SPLASH_OBS_LABEL_ATTR);
        tvram_putchar_at(3, 0, '0' + n % 10, SPLASH_OBS_LABEL_ATTR);
        int missing = vfs_stat(path, &stat);
        obs_start(n, missing ? 0 : obs_file_crc(path));
        if (missing) goto done;
        status = "no_sync";
        if (!rounds) goto done;
        status = "framebuffer";
        if (gfx_kernel_framebuffer(&fb)) goto done;
        status = "memory";
        image = kmalloc(sizeof(*image));
        plan = kmalloc(sizeof(*plan));
        if (!image || !plan) goto done;
        status = "invalid";
        if (osri_load(path, &fb, image)) goto done;
        if (image->version != OSRI_VERSION_2 || image->band != 1) goto done;
        status = "colors_mismatch";
        if (pl_obs_prepare(image, (u8)n, plan)) goto done;
        obs_black();
        gfx_add_dirty_rect(0, 0, GFX_WIDTH, GFX_HEIGHT);
        gfx_present_nosync();
        status = "no_sync";
        if (pl_preview(s)) { obs_black(); goto done; }
        status = "ok";
        for (u32 round = 0; round < rounds; round++) {
            int rc = pl_obs_frame(s, image, plan);
            if (rc == PL_FRAME_OK) frames++;
            else if (rc == PL_FRAME_DROP) drops++;
            else { status = "no_sync"; break; }
        }
        if (!frames && drops) status = "drop";
        obs_black();
done:
        if (plan) kfree(plan);
        if (image) kfree(image);
        obs_end(n, status, frames, drops);
    }
}
#endif
