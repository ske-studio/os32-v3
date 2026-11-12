/* PowerPC core-ready init (milestone 2, PPC_KBOOT_DESIGN sections 2 and 4).
 *
 * Entered from arch/ppc/kernel_boot.c with IR=DR=1, EE=0, plan-D paging live
 * and the ESCC/MPIC I/G aliases mapped. Brings the common mm online, proves
 * trap/guard/flush, MPIC delivery and the 1 kHz decrementer with real
 * operations, then idles with EE=1. The nine KBOOT lines are the acceptance
 * contract read by tools/ppc/kboot_run.py; every pass word below is computed.
 *
 * Not PC-98 init/kernel.c: no VFS, root mount, module load, exec_init or
 * user entry (those are milestone 3). Nothing here claims those services. */
#include "kernel/time/tick_clock.h"
#include "types.h"
#include "memmap.h"
#include "kprintf.h"
#include "kernel_boot.h"
#include "kernel_probe.h"
#include "kboot_info.h"
#include "kboot_log.h"
#include "lan_boot.h"
#include "mpic.h"
#include "ppc_trap.h"
#include "ppc_oea.h"
#include "ppc_hpt.h"
#include "arch_cpu.h"
#include "paging.h"
#include "memory_boot.h"
#include "pgalloc.h"
#include "kmalloc.h"
#include "shm.h"
#include "clock_source.h"
#include "kernel_tick.h"

#if KBOOT_SHELL || KBOOT_SH
#include "root_boot.h"
#if KBOOT_SH
#include "shell_boot.h"
#include "input_boot.h"
#include "hostfs.h"
#else
#include "cli_boot.h"
#endif
#include "exec_boot.h"
#include "exec.h"
extern u32 sys_mem_kb;
void pm_root_image(struct boot_root_image *out);
#endif

#ifndef KBOOT_NEGATIVE
#define KBOOT_NEGATIVE 0
#endif
#define KBOOT_MEM_KB 16384U
#define KBOOT_ATTR 0x07

/* Observation ABI for the acceptance meter (big-endian, always mapped). */
volatile unsigned long long kboot_ready_tb;

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
    if (!ppc_trap_onfault((u32)ppc_probe_read_insn, va, PPC_TRAP_DSI_MISS,
                          (u32)ppc_probe_read_resume)) return 0;
    int faulted = ppc_probe_read(va);
    ppc_trap_onfault_clear();
    return faulted == 1;
}
static int probe_isi(u32 va)
{
    if (!ppc_trap_onfault(va, va, PPC_TRAP_ISI, (u32)ppc_probe_exec_resume)) return 0;
    int faulted = ppc_probe_exec(va);
    ppc_trap_onfault_clear();
    return faulted == 1;
}
/* Remap one VA between two frames with the common mm call. paging_set_page()
 * ends in arch_mmu_flush_tlb() (mm/paging_map.inc); with plan D that must drop
 * the HPT entry and the TLB, or the old frame stays visible through the VA. */
static int probe_flush(void)
{
    volatile unsigned char *va = flush_a;
    flush_a[0] = 0xA5; flush_b[0] = 0x5B;
    if (*va != 0xA5) return 0;                       /* HPT entry for A is live */
    if (paging_set_page((u32)flush_a, (u32)flush_b, PAGE_RW) != 0) return 0;
    int moved = *va == 0x5B;
    if (paging_set_page((u32)flush_a, (u32)flush_a, PAGE_RW) != 0) return 0;
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

void ppc_kernel_main(void)
{
    const struct ppc_boot_devices *dev = ppc_boot_devices();
    const struct pm_kboot_info *info = ppc_boot_info();
    if (!info || pm_log_escc_init(dev->escc_csr_va, dev->escc_data_va) != 0)
        arch_cpu_stop();   /* no sink: nothing may be printed */

    int lan_log_rc = pm_lan_log_register();

    unsigned long long ram = 0;
    for (u32 i = 0; i < info->ram.count; i++) ram += info->ram.v[i].size;
    kprintf(KBOOT_ATTR, "KBOOT OF-CAPTURE ram=%u admitted=%u tb_hz=%u\r\n",
            (u32)ram, info->admitted_end, info->tb_hz);
    kprintf(KBOOT_ATTR, "KBOOT OF-OFF calls=%u\r\n", info->of_calls_at_detach);
    kprintf(KBOOT_ATTR, "KBOOT MMU-D root=0x%x hpt=0x%x bytes=%u ram_bat=%u\r\n",
            arch_mmu_current_root(), info->hpt_pa, info->hpt_bytes, bat_valid());

    /* Common mm: allocator/ledger, shared region, kernel heap (x86 order of
     * memory_boot_init -> shm_init; kmalloc is independent of both). */
    lan_boot = info->gmac;
    if (dev->gmac_ready) {
        struct pm_pci pci = {dev->pci_addr_va, dev->pci_data_va,
                             lan_boot.bus_first, lan_boot.bus_last, lan_boot.bdf};
        pm_gmac_quiesce_setup(&lan_boot, &pci);
        if (pm_gmac_early_quiesce() < 0) kboot_stop("gmac_quiesce");
    }
    if (!memory_boot_init(KBOOT_MEM_KB)) kboot_stop("memory_boot_init");
    shm_init();
    /* Same contract as x86 kernel_boot_memory(): shm_free/shm_lock toggle RW
     * on the *shared USER* mapping that paging_boot_user_shared() establishes
     * (mm/paging_map.inc). Its second argument is exec's trampoline page on
     * x86; milestone 2 has no exec, so a dedicated, never-executed, read-only
     * zero page stands in until exec/ provides the real one (milestone 3). */
#if KBOOT_SHELL || KBOOT_SH
    /* S-B: the shared USER page is exec's KAPI trampoline, as on x86. */
    sys_mem_kb = KBOOT_MEM_KB;
    int shared_user = paging_boot_user_shared(exec_tramp_page_addr()) == 0;
#else
    int shared_user = paging_boot_user_shared((u32)user_gate) == 0;
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
    kprintf(KBOOT_ATTR, "KBOOT MEMORY online=%u heap=%u shm=%u\r\n",
            (u32)(pgalloc_model_state() == PGALLOC_ONLINE && pgalloc_free_pages() > 0),
            (u32)heap_ok, (u32)shm_ok);

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
    kprintf(KBOOT_ATTR, "KBOOT TRAP dsi=%s isi=%s guard=%s flush=%s\r\n",
            dsi ? "pass" : "bad", isi ? "pass" : "bad",
            guard ? "pass" : "bad", flush ? "pass" : "bad");

    /* The decrementer handler must exist before EE is first enabled: a DEC
     * exception left pending by firmware would otherwise be unregistered. */
    pm_clock_bind(info);
    if (platform_clock_init(pm_tick_hz()) != 0) kboot_stop("clock_init");
    if (ppc_dec_start(info->tb_hz, pm_tick_hz()) != 0) kboot_stop("dec_start");

    if (pm_mpic_init(dev->mpic_va, dev->mpic_bytes, dev->mpic_order) != PM_MPIC_INIT_OK)
        kboot_stop("mpic_init");
    pm_mpic_set_ipi_handler(ipi_handler);
    ppc_enable();
    int delivered = pm_mpic_send_self_ipi(0) &&     /* 1 = sent */
                    wait_until(&ipi_seen, 1, info->tb_hz, 200) &&
                    pm_mpic_send_self_ipi(0) &&
                    wait_until(&ipi_seen, 2, info->tb_hz, 200) && ipi_seen == 2;
    kprintf(KBOOT_ATTR, "KBOOT MPIC sources=%u priority=0 cpu=0 delivery=%s\r\n",
            pm_mpic_source_count(), delivered ? "pass" : "bad");

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
    kprintf(KBOOT_ATTR, "KBOOT CLOCK hz=%u tick=%u us=%u\r\n", OS32_TICK_HZ, ticks, us_lo);

    if (dev->gmac_ready && lan_log_rc == 0) {
        pm_gmac_boot_id(&lan_boot, tb_now(), ppc_read_pvr());
        (void)pm_lan_start(&lan_boot);
    } else {
        kprintf(KBOOT_ATTR, "LAN OFF code=%d\r\n",
                dev->gmac_ready ? lan_log_rc : LAN_ENODEV);
    }

    unsigned long long start = tb_now();
    u32 tick0 = tick_count;
    kboot_ready_tb = start;
    kprintf(KBOOT_ATTR, "KBOOT READY stage=pre-user\r\n");
    unsigned long long now;
    do { now = tb_now(); pm_lan_poll(now); } while (now - start < 2ULL * info->tb_hz);
    u32 tick_delta = tick_count - tick0;
    unsigned long long delta = now - start;
    u32 tb_us = (u32)(delta / info->tb_hz * 1000000 + delta % info->tb_hz * 1000000 / info->tb_hz);
    kprintf(KBOOT_ATTR, "KBOOT ALIVE tick_delta=%u tb_us=%u of_calls_delta=%u\r\n",
            tick_delta, tb_us, ofw_call_count() - info->of_calls_at_detach);
#if KBOOT_SHELL || KBOOT_SH
    struct boot_root_image img;
    pm_root_image(&img);
    if (pm_root_boot(&img) == 0) {
#if KBOOT_SH
        (void)pm_hostfs_boot(info->tb_hz);
        pm_input_boot();
#endif
        pm_exec_boot();
#if KBOOT_SH
        pm_shell_boot();
#else
        pm_cli_boot();
#endif
    }
#endif
    for (;;) pm_lan_poll(tb_now());
}
