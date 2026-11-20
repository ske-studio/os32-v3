#include "platform_ver.h"
#include "boot_diag.h"
#include "cpu.h"
#include "gfx_hal.h"
#include "config.h"
#include "kstring.h"
#include "module/module.h"
#include "input_service.h"
#include "bootinfo.h"
#include "pcm_contract.h"

static void pc98_ver_collect(VerSnapshot *s)
{
    struct boot_diag diag;

    s->fpu_present = arch_fp_present();
    platform_boot_diag(&diag);
    s->timer_kind = diag.timer_kind;
    s->pit_valid = diag.timer_valid;
    s->pit_hz = diag.timer_hz;
    s->pit_input = diag.timer_input;
    s->irq_master_vector = diag.irq_master_vector;
    s->irq_slave_vector = diag.irq_slave_vector;
    s->pcm_present = pcm_present();
    services_ver_state(&s->services);
}

/* Internal backend values specified by VER_INFO_DESIGN §3. */
#define VER_GFX_PC98 1u
#define VER_GFX_PEGC 2u
#define VER_GFX_CIRRUS 3u
#define VER_GFX_GEFORCE 4u
#define VER_GFX_TRIDENT GFX_VER_TRIDENT
#define VER_VERSION_MAX 20u
#define VER_DEVICE (OS32_VER_DEVICE | OS32_VER_INDENT)
STATIC_ASSERT(sizeof(SYS_VERSION) - 1 <= VER_VERSION_MAX, ver_version_length);

static void __attribute__((cold)) pc98_ver_devices(const VerSnapshot *s, OS32_VerInfo *out)
{
    VerLineBuilder b;
    const char *backend = s->gfx_kind == VER_GFX_PC98 ? "98" :
                          s->gfx_kind == VER_GFX_PEGC ? "PEGC" :
                          s->gfx_kind == VER_GFX_CIRRUS ? "Cirrus" :
                          s->gfx_kind == VER_GFX_GEFORCE ? "GeForce" :
                          s->gfx_kind == VER_GFX_TRIDENT ? "Trident" : NULL;
    b = ver_line(out, VER_DEVICE, "CPU");
    ver_text(&b, "x86 32-bit; calib=");
    ver_text(&b, s->cpu_measured ? "measured" : "default");
#ifdef OS32_X87_EMU_LINKED
    ver_text(&b, s->fpu_present ? "; FPU=hw" : "; FPU=none(emu)");
#else
    ver_text(&b, s->fpu_present ? "; FPU=hw" : "; FPU=none");
#endif
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "PIC");
    ver_text(&b, "8259A x2; vectors=");
    ver_number(&b, s->irq_master_vector, 2);
    ver_text(&b, "h/");
    ver_number(&b, s->irq_slave_vector, 2);
    ver_text(&b, "h (configured)");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "PIT");
    if (s->pit_valid && s->timer_kind == BOOT_TIMER_8254) {
        ver_text(&b, "8254 tick="); ver_number(&b, s->pit_hz, 0);
        ver_text(&b, "Hz input="); ver_number(&b, s->pit_input, 0);
        ver_text(&b, "Hz");
    } else ver_text(&b, "unknown");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "KBD");
    ver_text(&b, "8251-compatible; IRQ"); ver_number(&b, input_irq(), 0);
    ver_text(&b, " (configured)");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "SER");
    if (!s->ser_ready) ver_text(&b, "not initialized");
    else if (s->ser_mode != KAPI_SER_MODE_COMPAT && s->ser_mode != KAPI_SER_MODE_VFAST)
        ver_text(&b, "unknown");
    else {
        ver_text(&b, "8251 baud="); ver_number(&b, s->ser_baud, 0);
        ver_text(&b, s->ser_mode == KAPI_SER_MODE_VFAST ? " mode=VFAST FIFO=" : " mode=compat FIFO=");
        ver_text(&b, s->ser_fifo ? "yes" : "no");
    }
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "SND");
    ver_text(&b, "OPN configured; PCM=");
    ver_text(&b, s->pcm_present ? "CS4231-family" : "none");
    ver_end(&b);
    b = ver_line(out, VER_DEVICE, "GFX");
    ver_gfx_value(&b, s, backend);
    ver_end(&b);
}

static void __attribute__((cold)) pc98_ver_image(const VerSnapshot *s, OS32_VerInfo *out)
{
    VerLineBuilder b;
    b = ver_line(out, OS32_VER_IDENTITY | OS32_VER_INDENT, "Image CRC");
    if (s->boot.crc_valid) {
        ver_number(&b, s->boot.image_crc, HEX32_DIGITS);
        ver_text(&b, " ("); ver_number(&b, s->boot.image_size, 0);
        ver_text(&b, " bytes, ");
        ver_text(&b, s->boot.source == BOOTINFO_SRC_HDD ? "HDD" :
                     s->boot.source == BOOTINFO_SRC_FD ? "FD" : "?");
        ver_text(&b, " loader)");
    } else ver_text(&b, "none (loader did not record)");
    ver_end(&b);
}

static void __attribute__((cold)) pc98_ver_debug(const VerSnapshot *s, OS32_VerInfo *out)
{
    VerLineBuilder b;
    b = ver_line(out, OS32_VER_DEBUG, "kdebug");
    if (!s->debug_ok) ver_text(&b, "unknown");
    else if (s->debug.kdebug_state == KDEBUG_OFF) ver_text(&b, "off");
    else if (s->debug.kdebug_state == KDEBUG_RUNNING) {
        ver_text(&b, "on (pair ");
        ver_number(&b, s->debug.kdebug_pair_crc, HEX32_DIGITS);
        ver_text(&b, ")");
    } else {
        ver_text(&b, "rejected "); ver_number(&b, s->debug.kdebug_state, 0);
        ver_text(&b, " ");
        ver_text(&b, kdebug_state_name((int)s->debug.kdebug_state));
    }
    ver_end(&b);
    if (s->debug_ok && s->debug.kdebug_state == KDEBUG_RUNNING) {
        b = ver_line(out, OS32_VER_DEBUG, "kdebug pages");
        ver_text(&b, "body="); ver_number(&b, s->debug.kdebug_pages, 0);
        ver_text(&b, " owner="); ver_number(&b, s->debug.kdebug_owner_pages, 0);
        ver_end(&b);
    } else if (s->debug_ok && s->debug.kdebug_state != KDEBUG_OFF) {
        b = ver_line(out, OS32_VER_DEBUG, "kdebug file pair");
        ver_number(&b, s->debug.kdebug_pair_crc, HEX32_DIGITS);
        ver_end(&b);
    }
    b = ver_line(out, OS32_VER_DEBUG, "sqlite stack high-water");
    /* This legacy diagnostic has '=' rather than ': '. */
    b.used -= 2;
    ver_text(&b, "="); ver_number(&b, s->services.stack_used, 0);
    ver_text(&b, " of "); ver_number(&b, s->services.stack_size, 0);
    ver_end(&b);
    b = ver_line(out, OS32_VER_DEBUG | OS32_VER_INDENT, "services");
    ver_saved_text(&b, s->services.state, sizeof(s->services.state));
    ver_text(&b, " public="); ver_number(&b, s->services.public, 0);
    ver_end(&b);
}

const struct ver_backend pc98_ver_backend = {
    .head = "PC-9821 OS32 v" SYS_VERSION " (Ring3 Native)",
    .collect = pc98_ver_collect,
    .devices = pc98_ver_devices,
    .image = pc98_ver_image,
    .debug = pc98_ver_debug,
};
