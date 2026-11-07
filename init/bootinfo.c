#include "bootinfo.h"
#include "kstring.h"
#include "kprintf.h"
#include "os32_kapi_shared.h"
#include "build_id.h"
static struct boot_common s_common;
static u8 s_source;
const struct boot_common *boot_common_get(void)
{
    return &s_common;
}
void boot_common_set(const struct boot_common *info, u8 source)
{
    s_common = *info;
    s_source = source;
}
/* kapi/kapi_sys.c (__DATE__ __TIME__ はそこだけが毎回組み直される) */
extern void kapi_sys_get_build_info(char *buf, int size);

int boot_image_info(void *out_v)
{
    BootImageInfo *out = (BootImageInfo *)out_v;

    if (!out) return OS32_ERR_INVAL;
    kmemset(out, 0, sizeof(*out));
    out->source = s_source;
    if (s_common.image_valid) {
        out->crc_valid  = 1;
        out->image_crc  = s_common.image_crc;
        out->image_size = s_common.image_size;
    }
    kstrncpy(out->commit, os32_build_commit, (u32)sizeof(out->commit));
    out->commit[sizeof(out->commit) - 1] = '\0';
    return 0;
}

void bootinfo_report_image(void)
{
    char build[BOOT_BUILD_TEXT_MAX];

    kapi_sys_get_build_info(build, (int)sizeof(build));
    kprintf(KLOG_ATTR, "[boot] Build: %s  Commit: %s\n", build, os32_build_commit);
    if (s_common.image_valid) {
        kprintf(KLOG_ATTR, "[boot] Image CRC: %08x (%u bytes, src=%s)\n",
                s_common.image_crc, s_common.image_size,
                s_source == BOOTINFO_SRC_HDD ? "hdd" :
                s_source == BOOTINFO_SRC_FD ? "fd" : "?");
    } else {
        kprintf(KLOG_ATTR, "[boot] Image CRC: none (loader did not record)\n");
    }
}
