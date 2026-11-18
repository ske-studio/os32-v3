#include "platform_sys.h"
#include "arch/ppc/kernel_boot.h"
#include "drivers/macintosh/via.h"
#include "io.h"
#include "kboot_info.h"
#include "kprintf.h"
#include "via_window.h"

static struct via_controller controller;
static int boot_error = PM_VIA_NOT_READY;

static u8 via_read(void *cookie, u32 offset)
{
    u8 value = ((volatile u8 *)cookie)[offset];
    __asm__ volatile("eieio" ::: "memory");
    return value;
}

static void via_write(void *cookie, u32 offset, u8 value)
{
    ((volatile u8 *)cookie)[offset] = value;
    __asm__ volatile("eieio" ::: "memory");
}

static unsigned long long via_tb(void *cookie)
{
    (void)cookie;
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((unsigned long long)hi << 32) | lo;
}

void pm_via_prepare(void)
{
    const struct pm_kboot_info *info = ppc_boot_info();
    const struct pm_via_window *window = pm_via_window();
    enum via_kind kind = info->via.kind == PM_VIA_PMU ? VIA_PMU :
                         info->via.kind == PM_VIA_CUDA ? VIA_CUDA : VIA_NONE;
    const char *name = kind == VIA_PMU ? "pmu" : kind == VIA_CUDA ? "cuda" : "none";
    controller = (struct via_controller){
        .kind = kind,
        .io = { (void *)window->va, via_read, via_write, via_tb, info->tb_hz },
    };
    boot_error = window->rc;
    if (!boot_error) boot_error = via_prepare(&controller);
    if (boot_error)
        kprintf(7, "VIA kind=%s pa=%08x va=%08x rc=%d compatible=%s\r\n",
                name, info->via.pa, window->va, boot_error, info->via.compatible);
    else
        kprintf(7, "VIA kind=%s pa=%08x va=%08x rc=ok compatible=%s\r\n",
                name, info->via.pa, window->va, info->via.compatible);
}

void platform_reset(void)
{
    _disable();
    int rc = boot_error ? boot_error : via_restart(&controller);
    if (!rc) return;
    kprintf(7, "VIA reset rc=%d\r\n", rc);
    unsigned long long start = via_tb(0), now = start;
    while (now - start < 2ULL * controller.io.tb_hz) {
        pm_lan_poll(now);
        now = via_tb(0);
    }
}
