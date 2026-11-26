#include "platform_trap_sink.h"
#include "arch_io.h"
#include "platform_machcfg.h"
#include "ide_boot.h"
#include "loop_boot.h"
#include "sqlite_engine.h"
#include "fep_boot.h"
#include "platform_module_pair.h"
#include "boot_token.h"
/* PowerPC core-ready init (milestone 2, PPC_KBOOT_DESIGN sections 2 and 4).
 *
 * Entered through sys/kern/init/kernel_start.c (the common three-phase skeleton) from
 * sys/platform/powermac/kernel_boot.c with IR=DR=1, EE=0, plan-D paging live
 * and the ESCC/MPIC I/G aliases mapped. Brings the common mm online, proves
 * trap/guard/flush, MPIC delivery and the 1 kHz decrementer with real
 * operations, then idles with EE=1. The nine KBOOT lines are the acceptance
 * contract read by kboot_run.py (旧版 3c4a2c5ec78d); every pass word below is computed.
 *
 * Not PC-98 sys/platform/pc98/kernel.c: no VFS, root mount, module load, exec_init or
 * user entry (those are milestone 3). Nothing here claims those services. */
#include "sys/hal/time/tick_clock.h"
#include "types.h"
#include "memmap.h"
#include "platform_kboot_layout.h"
#include "kprintf.h"
#include "kernel_boot.h"
#include "kernel_start.h"
#include "kernel_probe.h"
#include "hwb_window.h"
#include "hwb_resource.h"
#include "kboot_info.h"
#include "kboot_report.h"
#include "via_window.h"
#include "kboot_log.h"
#include "lan_boot.h"
#include "mpic.h"
#include "ppc_trap.h"
#include "ppc_oea.h"
#include "ppc_hpt.h"
#include "arch_cpu.h"
#include "paging.h"
#include "rh_observe.h"
#include "sys/dev/net/gem/gem.h"
#include "memory_boot.h"
#include "pgalloc.h"
#include "physmem.h"
#include "kmalloc.h"
#include "shm.h"
#include "clock_source.h"
#include "kernel_tick.h"

#if KBOOT_SHELL || KBOOT_SH
#include "root_boot.h"
#include "kernel_mount_children.h"
#include "pm_console.h"
#include "pointer_service.h"
#include "console_backend.h"
#include "unicode_file.h"
#include "display_text.h"
#include "sys/kern/console/font_source_kcg.h"
#include "sys/hal/gfx/gfx.h"
#include "sys/kern/console/gfx_console.h"
#include "sys/hal/gfx/v2/gfx2_int.h"
#include "config.h"
#include "console.h"
#include "text_screen.h"
#if KBOOT_SH
#include "shell_loop_ops.h"
#include "input_boot.h"
#include "hostfs.h"
#include "usb_boot.h"
#if KBOOT_LINK
#include "sys/platform/powermac/link_platform.h"
#include "sys/kern/idle_work.h"
#endif
#endif
#include "exec_boot.h"
#include "exec.h"
extern u32 sys_mem_kb;
int pm_root_image(struct boot_root_image *image, u32 frame_bytes);
#endif

int platform_boot_staging_in_use(void)
{
#if OS32_FEATURE_IDE && !OS32_HOOK_IME
    if (pm_root_unit != -1) return 0;
#endif
    return ppc_boot_info()->root_frame_bytes != 0;
}

#ifndef KBOOT_NEGATIVE
#define KBOOT_NEGATIVE 0
#endif
#define KBOOT_MEM_KB 16384U
#define KBOOT_ATTR 0x07

static struct pm_gmac_boot lan_boot;
static volatile u32 ipi_seen;
static unsigned char flush_a[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
static unsigned char flush_b[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
#if !KBOOT_SHELL && !KBOOT_SH
static unsigned char user_gate[PAGE_SIZE] __attribute__((aligned(PAGE_SIZE)));
#endif

static void kboot_stop(const char *why) __attribute__((noreturn));
static void kboot_stop(const char *why)
{
    kprintf(KBOOT_ATTR, "FATAL kboot %s\r\n", why);
    arch_cpu_stop();
}

#include "pci_boot_memory.inc"

static unsigned long long tb_now(void)
{
    u32 hi, lo, again;
    do {
        __asm__ volatile("mftbu %0" : "=r"(hi));
        __asm__ volatile("mftb %0" : "=r"(lo));
        __asm__ volatile("mftbu %0" : "=r"(again));
    } while (hi != again);
    return ((unsigned long long)hi << 32) | lo;
}

/* D2 provider. Line 1 is the complete, build-independent verdict (the
 * negative-image contract matches it exactly); line 2 is the variable frame.
 * addr is DAR for a DSI, SRR0 for an ISI, 0 for vectors without a fault address. */
void ppc_trap_fatal_sink(const struct ppc_frame *f, u32 reason)
{
    u32 addr = f->vector == 0x300 ? f->dar : f->vector == 0x400 ? f->srr0 : 0;
    kprintf(KBOOT_ATTR, "FATAL trap vector=0x%x reason=0x%x addr=0x%x\r\n",
            f->vector, reason, addr);
    kprintf(KBOOT_ATTR, "  frame srr0=0x%x srr1=0x%x lr=0x%x sp=0x%x\r\n",
            f->srr0, f->srr1, f->lr, f->gpr[1]);
}

/* Upper-BAT valid bits (Vs|Vp) still set anywhere: 0 after C4's retirement. */
#define BAT_V(spr) ({ u32 v_; __asm__ volatile("mfspr %0,%1" : "=r"(v_) : "i"(spr)); v_ & 3U; })
static u32 bat_valid(void)
{
    u32 n = BAT_V(PPC_SPR_IBAT0U) + BAT_V(PPC_SPR_IBAT1U) + BAT_V(PPC_SPR_IBAT2U) +
            BAT_V(PPC_SPR_IBAT3U) + BAT_V(PPC_SPR_DBAT0U) + BAT_V(PPC_SPR_DBAT1U) +
            BAT_V(PPC_SPR_DBAT2U) + BAT_V(PPC_SPR_DBAT3U);
    /* BAT4-7 exist only on the 7455 with HID0[HIGH_BAT_EN]. */
    if (ppc_read_pvr() >> 16 == PPC_PVR_7455 && (ppc_read_hid0() & PPC_HID_HIGHBAT))
        n += BAT_V(PPC_SPR_IBAT4U) + BAT_V(PPC_SPR_IBAT5U) + BAT_V(PPC_SPR_IBAT6U) +
             BAT_V(PPC_SPR_IBAT7U) + BAT_V(PPC_SPR_DBAT4U) + BAT_V(PPC_SPR_DBAT5U) +
             BAT_V(PPC_SPR_DBAT6U) + BAT_V(PPC_SPR_DBAT7U);
    return n;
}

static int probe_dsi(u32 va)
{
    u32 flags = ppc_irq_save();
    if (!ppc_trap_onfault((u32)ppc_probe_read_insn, va, PPC_TRAP_DSI_MISS,
                          (u32)ppc_probe_read_resume)) { ppc_irq_restore(flags); return 0; }
    int faulted = ppc_probe_read(va);
    ppc_trap_onfault_clear();
    ppc_irq_restore(flags);
    return faulted == 1;
}
static int probe_isi(u32 va)
{
    u32 flags = ppc_irq_save();
    if (!ppc_trap_onfault(va, va, PPC_TRAP_ISI, (u32)ppc_probe_exec_resume)) { ppc_irq_restore(flags); return 0; }
    int faulted = ppc_probe_exec(va);
    ppc_trap_onfault_clear();
    ppc_irq_restore(flags);
    return faulted == 1;
}
/* Remap one VA between two frames with the common mm call. paging_set_page()
 * ends in arch_mmu_flush_tlb() (sys/kern/mm/paging_map.inc); with plan D that must drop
 * the HPT entry and the TLB, or the old frame stays visible through the VA. */
static int probe_flush(void)
{
    volatile unsigned char *va = flush_a;
    flush_a[0] = 0xA5; flush_b[0] = 0x5B;
    if (*va != 0xA5) return 0;                       /* HPT entry for A is live */
    if (paging_set_page((u32)flush_a, V2P(flush_b), PAGE_RW) != 0) return 0;
    int moved = *va == 0x5B;
    if (paging_set_page((u32)flush_a, V2P(flush_a), PAGE_RW) != 0) return 0;
    return moved && *va == 0xA5;
}

static void ipi_handler(struct ppc_frame *f) { (void)f; ++ipi_seen; }
static int wait_until(volatile u32 *value, u32 target, u32 tb_hz, u32 ms)
{
    unsigned long long end = tb_now() + (unsigned long long)tb_hz / 1000 * ms;
    while (*value < target)
        if (tb_now() >= end) return 0;
    return 1;
}

/* RH2: show the sampled PHY ID on the FB band (real G4 PHY unknown until sampled). */
static void rh_phy_notify(const struct gem_phy_sample *p, int rc)
{
    (void)rc;
    pm_rh_phy(p->port, p->addr, p->id1, p->id2);
}

static int lan_log_rc;
#if KBOOT_SHELL || KBOOT_SH
static int root_ready;
#if OS32_HOOK_IME && OS32_FEATURE_IDE
static int hdd_root_ready;
#endif
#endif

#if KBOOT_SHELL_LAYOUT_DIAG
#include "ppc_pd.h"

static u32 layout_read(u32 pa, void *unused)
{
    (void)unused;
    return *(const u32 *)P2V(pa);
}

static void report_regions(const char *name, const struct pm_regions *regions)
{
    for (unsigned i = 0; i < regions->count; i++) {
        const struct pm_region *r = &regions->v[i];
        kprintf(KBOOT_ATTR, "KBOOT OF-RANGE %s %u %u\r\n",
                name, r->start, r->start + r->size);
    }
}

static void shell_layout_report(const struct pm_kboot_info *info)
{
    u32 root = arch_mmu_current_root();
    u32 resident_end = (u32)&__kernel_resident_end;
    report_regions("reg", &info->ram);
    report_regions("available", &info->available);
    kprintf(KBOOT_ATTR, "KBOOT OF-IMAGE end=%u reserved=%u reg=%u available=%u\r\n",
            resident_end, info->image_end - resident_end, info->ram.count, info->available.count);
    unsigned image = 0, heap = 0, stack = 0, guard = 0, available = 0;
    for (u32 va = MEM_SHELL_LOAD_ADDR; va < MEM_SHELL_STACK_TOP; va += PAGE_SIZE) {
        struct ppc_translation t;
        u32 end = va + PAGE_SIZE, cursor = va;
        for (unsigned i = 0; i < info->available.count && cursor < end; i++) {
            const struct pm_region *r = &info->available.v[i];
            if (r->start > cursor) break;
            if (r->start + r->size > cursor) cursor = r->start + r->size;
        }
        available += cursor >= end;
        int mapped = ppc_pd_lookup_read(root, va, 1, 0, &t, layout_read, NULL);
        if (va == MEM_SHELL_GUARD) {
            guard = !mapped && !(paging_pte_raw(va) & PTE_PRESENT);
            continue;
        }
        if (!mapped || t.pa != va || t.attr != 0x10 ||
            !(paging_pte_raw(va) & PTE_RW)) continue;
        if (va < MEM_SHELL_HEAP_BASE) image++;
        else if (va < MEM_SHELL_GUARD) heap++;
        else stack++;
    }
    kprintf(KBOOT_ATTR, "KBOOT SHELL-LAYOUT image=%u heap=%u guard=%u stack=%u available=%u\r\n",
            image, heap, guard, stack, available);
}
#endif

/* 局面 1 (EE=0): ログ・共通の mm・トラップの実証・時計・MPIC。 */
void platform_boot_machine(void)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    const struct pm_kboot_info *info = ppc_boot_info();
    pm_rh_bind(dev->fb_va);
    pm_rh_stage(1);
    if (!info || pm_log_escc_init(dev->escc_csr_va, dev->escc_data_va) != 0)
    {
        pm_rh_stage(PM_RH_ESCC_INIT);
        arch_cpu_stop();
    }

#if KBOOT_SHELL || KBOOT_SH
    pm_console_stream_init();
#endif
    lan_log_rc = pm_lan_log_register();

    unsigned long long ram = 0;
    for (u32 i = 0; i < info->ram.count; i++) ram += info->ram.v[i].size;
    pm_report_handoff(info, (u32)ram, arch_mmu_current_root(), bat_valid());

    /* Common mm: allocator/ledger, shared region, kernel heap (x86 order of
     * memory_boot_init -> shm_init; kmalloc is independent of both). */
    lan_boot = info->gmac;
    pm_lan_phy_observer(rh_phy_notify);
    pci_boot_memory(info, dev);
    pm_hwb_window_init();
#if PM_LOAD_KDEBUG
    pm_hwb_resource_init(info, pm_gmac_rh_sample()->valid);
#endif
#if KBOOT_SHELL_LAYOUT_DIAG
    shell_layout_report(info);
#endif
    shm_init();
    /* Same contract as x86 kernel_boot_memory(): shm_free/shm_lock toggle RW
     * on the *shared USER* mapping that paging_boot_user_shared() establishes
     * (sys/kern/mm/paging_map.inc). Its second argument is exec's trampoline page on
     * x86; milestone 2 has no exec, so a dedicated, never-executed, read-only
     * zero page stands in until exec/ provides the real one (milestone 3). */
#if KBOOT_SHELL || KBOOT_SH
    /* S-B: the shared USER page is exec's KAPI trampoline, as on x86. */
    sys_mem_kb = KBOOT_MEM_KB;
    exec_kapi_init();
    int shared_user = ring3_trampoline_init() == 0 &&
                      paging_boot_user_shared(exec_tramp_page_addr(), 2) == 0;
#else
    int shared_user = paging_boot_user_shared((u32)user_gate, 1) == 0;
#endif
    if (!shared_user) kboot_stop("KAPI trampoline");
#if KBOOT_SHELL || KBOOT_SH
    /* The fixed 192 KiB heap cannot hold the 296 KiB resident font. Reserve
     * a separate RAM arena before any heap clients exist; leave KAPI/SHM's
     * fixed layout intact. Allocation failure retains the stream-only path. */
    u32 heap_pfn;
    if (pgalloc_alloc_n_owner(LEDGER_OWNER_BOOT, PM_CONSOLE_HEAP_BYTES / PAGE_SIZE,
            MEM_POOL_BASE / PAGE_SIZE, pgalloc_arena_end(), LEDGER_TOP_DOWN, &heap_pfn))
        kmalloc_init(P2V(heap_pfn * PAGE_SIZE), PM_CONSOLE_HEAP_BYTES);
    else
#endif
        kmalloc_init((void *)KHEAP_BASE, KHEAP_SIZE);
    void *heap = kmalloc(256);
    int heap_ok = heap && kmalloc_used() >= 256;
    if (heap) { *(volatile u32 *)heap = 0x4b424f54; kfree(heap); }
    /* A full round trip: the block must really return to the free state. */
    void *shared = shm_alloc(1);
    int shm_ok = shared_user && shared;
    if (shared) {
        *(volatile u32 *)shared = 0x4b424f54;
        shm_ok = shm_ok && shm_lock(shared) == 0 && shm_free(shared) == 0;
        void *again = shm_alloc(1);
        shm_ok = shm_ok && again == shared && *(volatile u32 *)again == 0 &&
                 shm_free(again) == 0;
    }
    u32 online = pgalloc_model_state() == PGALLOC_ONLINE && pgalloc_free_pages() > 0;
    if (!online || !heap_ok || !shm_ok || pm_report_successes) {
        kprintf(KBOOT_ATTR, "KBOOT MEMORY online=%u heap=%u shm=%u\r\n",
                online, heap_ok, shm_ok);
    }

#if KBOOT_NEGATIVE == 1
    (void)ppc_probe_read(0x80000);              /* unarmed low-guard fault */
    kboot_stop("negative-1 survived");
#elif KBOOT_NEGATIVE == 2
    ppc_probe_syscall();                        /* unsupported entry */
    kboot_stop("negative-2 survived");
#endif

    int dsi = probe_dsi(0x80000);               /* low 1MiB is not present */
    int isi = probe_isi(0x80000);
    int guard = probe_dsi(MEM_STACK_GUARD);     /* kernel stack guard page */
    int flush = probe_flush();
    if (!dsi || !isi || !guard || !flush || pm_report_successes) {
        kprintf(KBOOT_ATTR, "KBOOT TRAP dsi=%s isi=%s guard=%s flush=%s\r\n",
                dsi ? "pass" : "bad", isi ? "pass" : "bad",
                guard ? "pass" : "bad", flush ? "pass" : "bad");
    }

    /* The decrementer handler must exist before EE is first enabled: a DEC
     * exception left pending by firmware would otherwise be unregistered. */
    pm_clock_bind(info);
    if (platform_clock_init(pm_tick_hz()) != 0) kboot_stop("clock_init");
    if (ppc_dec_start(info->tb_hz, pm_tick_hz()) != 0) kboot_stop("dec_start");
    pm_via_prepare();

    if (pm_mpic_init(dev->mpic_va, dev->mpic_bytes, dev->mpic_order) != PM_MPIC_INIT_OK)
        kboot_stop("mpic_init");
    pm_mpic_set_ipi_handler(ipi_handler);
}

static void apply_machine_config(void)
{
    u32 pvr = ppc_read_pvr();
    struct machcfg_seed seed;
    ppc_cpu_power_apply(pvr, machcfg_get()->nap7450);
    machcfg_report("powermac", pvr, pm_machcfg_seed(pvr, &seed));
    u32 hid = ppc_read_hid0();
    kprintf(KBOOT_ATTR, "MACHCFG HID0 pvr=0x%x hid0=0x%x nap=%u\r\n",
            ppc_read_pvr(), hid, (unsigned)!!(hid & PPC_HID_NAP));
}

/* 局面 2 (EE=1): MPIC の配送と時計の実証・LAN・root・コンソール・入力。 */
#if KBOOT_SHELL || KBOOT_SH
#if !PM_PRODUCT_BOOT
static int start_media_root(const struct pm_kboot_info *info)
{
    struct boot_root_image img = {0};
    if (!info->root_frame_bytes) {
        machcfg_finish(0);
        apply_machine_config();
        return 0;
    }
    u32 owned_before = ledger_owner_pages(LEDGER_OWNER_BOOT);
    int registered = 0;
    int rc = pm_root_image(&img, info->root_frame_bytes);
    if (!rc) rc = pm_root_boot(&img, &registered);
    if (!rc) machcfg_load_root();
    else machcfg_finish(0);
    apply_machine_config();
    u32 pages = MEM_BOOT_STAGING_SIZE / PAGE_SIZE;
    u32 free_before, free_after;
    if (pm_root_release(&img, registered, PM_ROOT_FRAME_BASE / PAGE_SIZE, pages,
                        &free_before, &free_after))
        kboot_stop("root page release");
    kprintf(KBOOT_ATTR, "KBOOT ROOT rc=%d registered=%d bytes=%u staging_pages=%u\r\n",
            rc, registered, img.bytes, pages);
    kprintf(KBOOT_ATTR, "KBOOT ROOT owner_before=%u owner_after=%u free_before=%u free_after=%u\r\n",
            owned_before, ledger_owner_pages(LEDGER_OWNER_BOOT), free_before, free_after);
    return !rc;
}
#endif
#endif

void platform_boot_services(void)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    const struct pm_kboot_info *info = ppc_boot_info();
    int delivered = pm_mpic_send_self_ipi(0) &&     /* 1 = sent */
                    wait_until(&ipi_seen, 1, info->tb_hz, 200) &&
                    pm_mpic_send_self_ipi(0) &&
                    wait_until(&ipi_seen, 2, info->tb_hz, 200) && ipi_seen == 2;
    if (!delivered || pm_report_successes) {
        kprintf(KBOOT_ATTR, "KBOOT MPIC sources=%u priority=0 cpu=0 delivery=%s\r\n",
                pm_mpic_source_count(), delivered ? "pass" : "bad");
    }

    if (!wait_until((volatile u32 *)&tick_count, 1, info->tb_hz, 1000))
        kboot_stop("no tick");
    /* The clock provider must advance with the time base: read it twice
     * at least ~20 ms apart and compare its microseconds with the TB interval. */
    u32 us0 = 0, us_lo = 0, us_hi = 0, hi0 = 0;
    u32 raw0_lo, raw0_hi, raw1_lo, raw1_hi;
    u32 saved = ppc_irq_save();                 /* contract: read with IF=0 */
    unsigned long long tb0_a = tb_now();
    tick_clock_raw(&raw0_lo, &raw0_hi);
    int clock_ok = platform_clock_read_us(&us0, &hi0) == 0;
    unsigned long long tb0_b = tb_now();
    ppc_irq_restore(saved);
    u32 after = tick_count + 3;
    if (!wait_until((volatile u32 *)&tick_count, after, info->tb_hz, 1000))
        kboot_stop("no tick");
    saved = ppc_irq_save();
    unsigned long long tb1_a = tb_now();
    tick_clock_raw(&raw1_lo, &raw1_hi);
    clock_ok = platform_clock_read_us(&us_lo, &us_hi) == 0 && clock_ok;
    unsigned long long tb1_b = tb_now();
    u32 ticks = tick_count;
    ppc_irq_restore(saved);
    unsigned long long raw0 = ((unsigned long long)raw0_hi << 32) | raw0_lo;
    unsigned long long raw1 = ((unsigned long long)raw1_hi << 32) | raw1_lo;
    unsigned long long dec_want = (tb1_a - tb0_a) * PM_DEC_HZ / info->tb_hz;
    unsigned long long dec_got = raw1 - raw0;
    unsigned long long dec_error = dec_got > dec_want ? dec_got - dec_want : dec_want - dec_got;
    if (dec_error > dec_want / 50 + 2) kboot_stop("dec_rate");
    if (!clock_ok || hi0 || us_hi) kboot_stop("clock_read");
    u32 want = (u32)((tb1_b - tb0_a) * 1000000 / info->tb_hz);
    unsigned long long lower = (tb1_a - tb0_b) * 1000000 / info->tb_hz;
    unsigned long long upper = (tb1_b - tb0_a) * 1000000 / info->tb_hz;
    u32 got = us_lo - us0;
    /* Each endpoint's conversion can round down by one microsecond. */
    if (want < 10000 || (unsigned long long)got + 1 < lower ||
        (unsigned long long)got > upper + 1)
        kboot_stop("clock_rate");
    pm_report_clock(OS32_TICK_HZ, ticks, us_lo);

    pm_pci_enumerate();
    boot_token_init();
    boot_token_get(lan_boot.boot_id);
    if (dev->gmac_ready && lan_log_rc == 0) {
        (void)pm_lan_start(&lan_boot);
    } else {
        kprintf(KBOOT_ATTR, "LAN OFF code=%d\r\n",
                dev->gmac_ready ? lan_log_rc : LAN_ENODEV);
    }

    unsigned long long start = tb_now();
    u32 tick0 = tick_count;
    pm_report_ready(start);
    unsigned long long now;
    do { now = tb_now(); pm_lan_poll(now); } while (now - start < 2ULL * info->tb_hz);
    pm_report_alive(now, start, tick0, info);
#if KBOOT_SHELL || KBOOT_SH
    kprintf(KBOOT_ATTR, "FB full_rc=%d color_rc=%d\r\n",
            dev->fb_full_rc, dev->fb_color_rc);
#if OS32_FEATURE_IDE
    if (pm_root_unit != -1) {
        int registered;
        pm_ide_boot();
        int rc = pm_root_select();
        if (rc) {
            kprintf(KBOOT_ATTR, "HDD root unit=%d partition=%u select rc=%d\r\n",
                    pm_root_unit, pm_root_partition, rc);
            kboot_stop("hd0 select");
        }
        rc = pm_root_boot(0, &registered);
        if (rc) {
            kprintf(KBOOT_ATTR, "HDD root hd0 mount/path rc=%d\r\n", rc);
            kboot_stop("hd0 root");
        }
#if OS32_HOOK_IME
#if PM_PRODUCT_BOOT
        hdd_root_ready = 1;
#else
        /* Validate the embedded pair without registering it as the root. */
        struct boot_root_image paired_image = {0};
        if (info->root_frame_bytes) {
            int pair_rc = pm_root_image(&paired_image, info->root_frame_bytes);
            u32 before, after;
            if (pm_root_release(&paired_image, 0, PM_ROOT_FRAME_BASE / PAGE_SIZE,
                                MEM_BOOT_STAGING_SIZE / PAGE_SIZE, &before, &after))
                kboot_stop("HDD pair page release");
            hdd_root_ready = !pair_rc;
        }
#endif
#endif
        machcfg_load_root();
        apply_machine_config();
    } else
#endif
    {
#if PM_PRODUCT_BOOT
        kboot_stop("HDD boot path");
#else
        if (!start_media_root(info)) return;
#endif
        pm_ide_boot();
    }
    {
        pm_loop_boot();
        kernel_mount_children(vfs_devname("/"));
        (void)pm_console_text_start();
#if KBOOT_SH
        /* Before pm_usb_boot (mouse_init): the default must precede the cursor. */
        u32 fb_w, fb_h;
        pm_console_text_size(&fb_w, &fb_h);
        if (fb_w && fb_h)
            pointer_set_default_bounds(fb_w > 32768 ? 32767 : fb_w - 1,
                                       fb_h > 32768 ? 32767 : fb_h - 1);
        (void)pm_hostfs_boot(info->tb_hz);
        pm_usb_boot(info->tb_hz);
#if KBOOT_LINK
        int work_rc = idle_work_register(pm_link_service_once);
#else
        int work_rc = pm_terminal_work_register();
#endif
        if (work_rc) kprintf(KBOOT_ATTR, "TERMINAL WORK error rc=%d\r\n", work_rc);
        pm_input_boot();
#endif
        root_ready = 1;
    }
#else
    machcfg_finish(0);
    apply_machine_config();
#endif
}

/* 局面 3: exec の用意と常駐シェル。シェルが無い像・root が無い時は LAN の poll で居続ける。 */
void platform_boot_run(void)
{
#if KBOOT_SHELL || KBOOT_SH
    if (root_ready) {
#if OS32_HOOK_KAPI_DB
        /* Keep SQLite boot diagnostics on the registered log streams. */
        int sql_rc = sqlite_static_boot();
        kprintf(KBOOT_ATTR, "[sqlite] static rc=%d\n", sql_rc);
#if OS32_HOOK_IME && OS32_FEATURE_IDE
        u32 pair;
        if (!sql_rc && hdd_root_ready && unicode_file_debug_phys() && platform_module_pair(&pair))
            kprintf(KBOOT_ATTR, "[fep] static rc=%d\n", fep_boot_static());
#endif
#endif
        pm_exec_boot();
        shell_loop_run(&pm_shell_loop_ops);
    }
#endif
    for (;;) pm_lan_poll(tb_now());
}
