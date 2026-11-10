/* MPC7450UM tables 2-6/2-33, 5.3; PEM 7.7.1.
 * NetBSD oea/cpu_subr.c:309-331,357-374 (features),
 * oea/oea_machdep.c:713-722 (BAT retirement); FreeBSD aim/mmu_oea.c:685-695
 * and NetBSD oea/pmap.c:3570 (segments/SDR1 ordering).
 * Kernel takeover only: IR=DR=EE=0, identity physical stack/data.
 * No RAM BAT is installed. Caller diagnoses identify failure before takeover. */
#include "ppc_oea.h"
#include "ppc_hpt.h"

/* Host substitutes only privileged instructions, keeping product control flow. */
#ifdef PPC_CPU_HOST_TEST
void ppc_cpu_test_stream_stop(void);
#define CPU_DSSALL() ppc_cpu_test_stream_stop()
void ppc_cpu_test_spr(u32 spr, u32 value);
void ppc_cpu_test_sr(u32 sr, u32 value);
#define CPU_SPR(n, v) ppc_cpu_test_spr(n, v)
#define CPU_SR(n, v) ppc_cpu_test_sr(n, v)
#else
#define CPU_DSSALL() __asm__ volatile("dssall" ::: "memory")
#define CPU_SPR(n, v) __asm__ volatile("mtspr %0,%1" :: "i"(n), "r"(v) : "memory")
#define CPU_SR(n, v) __asm__ volatile("mtsr %0,%1" :: "i"(n), "r"(v) : "memory")
#endif

int ppc_cpu_identify(u32 *pvr)
{
    u32 value = ppc_read_pvr();
    if (pvr)
        *pvr = value; /* Raw PVR remains available for the caller's diagnostic. */
    return value >> 16 == PPC_PVR_7450 || value >> 16 == PPC_PVR_7455;
}

void ppc_cpu_disable_bats(u32 pvr)
{
    u32 version = pvr >> 16;
    if (version != PPC_PVR_7450 && version != PPC_PVR_7455)
        ppc_stop(); /* Never execute unsupported SPR accesses. */
    u32 hid = ppc_read_hid0();
    /* TBEN also needs the board's asserted TBEN pin (checked by later clock
     * discovery). Preserve firmware cache/prediction and extended-size bits. */
    hid = (hid | PPC_HID_TBEN) & ~PPC_HID_STEN;
    if (version == PPC_PVR_7455)
        hid |= PPC_HID_HIGHBAT | PPC_HID_XBSEN;
    /* Table 2-6 note 2: drain firmware data streams before changing STEN. */
    CPU_DSSALL();
    ppc_sync();
    ppc_write_hid0(hid);
    ppc_sync();
    ppc_isync();
    CPU_SPR(PPC_SPR_IBAT0U, 0); CPU_SPR(PPC_SPR_IBAT1U, 0);
    CPU_SPR(PPC_SPR_IBAT2U, 0); CPU_SPR(PPC_SPR_IBAT3U, 0);
    CPU_SPR(PPC_SPR_DBAT0U, 0); CPU_SPR(PPC_SPR_DBAT1U, 0);
    CPU_SPR(PPC_SPR_DBAT2U, 0); CPU_SPR(PPC_SPR_DBAT3U, 0);
    if (version == PPC_PVR_7455 && (hid & PPC_HID_HIGHBAT)) {
        CPU_SPR(PPC_SPR_IBAT4U, 0); CPU_SPR(PPC_SPR_IBAT5U, 0);
        CPU_SPR(PPC_SPR_IBAT6U, 0); CPU_SPR(PPC_SPR_IBAT7U, 0);
        CPU_SPR(PPC_SPR_DBAT4U, 0); CPU_SPR(PPC_SPR_DBAT5U, 0);
        CPU_SPR(PPC_SPR_DBAT6U, 0); CPU_SPR(PPC_SPR_DBAT7U, 0);
    }
    ppc_sync();
    ppc_isync();
}

void ppc_cpu_load_segments(void)
{
    ppc_sync();
    /* Ks=0, Kp=1, T=N=0, same fixed VSIDs as the kernel C2 provider. */
#define SEG(n) CPU_SR(n, PPC_SR_KP | ppc_hpt_vsid(n))
    SEG(0); SEG(1); SEG(2); SEG(3); SEG(4); SEG(5); SEG(6); SEG(7);
    SEG(8); SEG(9); SEG(10); SEG(11); SEG(12); SEG(13); SEG(14); SEG(15);
#undef SEG
    ppc_sync();
    ppc_isync();
    ppc_tlb_flush_all();
}

void ppc_cpu_load_sdr1(u32 sdr1)
{
    ppc_sync();
    CPU_SPR(PPC_SPR_SDR1, sdr1);
    ppc_sync();
    ppc_isync(); /* SDR1 context synchronization before any TLB use. */
    ppc_tlb_flush_all();
}
