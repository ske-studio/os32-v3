/* ======================================================================== */
/*  arch/x86/arch_cpu.h — アドレス空間の切替と特権境界の x86 実装           */
/*                                                                          */
/*  契約は include/cpu.h にある。ここは i386 での実現方法だけを書く。         */
/*  このファイルを直接 #include してはいけない — 常に "cpu.h" を引くこと。    */
/*  ビルドは -Iarch/$(ARCH) で解決する (build/config.mk)。                   */
/*                                                                          */
/*  元の命令列は移設前 (mm/paging.c・exec/exec.c のその場の asm) から         */
/*  1 文字も変えていない。移設の前後でカーネルの .o が md5 で一致することを   */
/*  確かめてある (docs/tasks/portability/ARM_GAUGE.md §9 順序 5)。           */
/*  T5-0b: CR3変更時にTSSの静的CR3欄も更新し、#DF診断用に控える。          */
/* ======================================================================== */

#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include "x86_flags.h"
#include "gdt.h"        /* USER_CS / USER_DS (CONTRACTS C1 で凍結したセレクタ) */
#include "tss.h"        /* kernel_tss.esp0 — CPL=3 からの trap が使うスタック */

/* x86 制御レジスタのビット。CPU の持ち物なので機種ヘッダ (pc98.h) ではなく
 * ここに置く (順序 5 で include/pc98.h から移した)。 */
#define CR0_PE              0x00000001UL  /* プロテクトモード有効 */
#define CR0_EM              0x00000004UL  /* x87 エミュレーション */
#define CR0_TS              0x00000008UL  /* タスク切替済み */
#define CR0_PG              0x80000000UL  /* ページング有効 */

/* ---- アドレス空間 (CR3 / CR0) ---- */

static inline u32 arch_mmu_current_root(void)
{
    u32 cr3_val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
    return cr3_val;
}

/* CR3 への書き込みは、同じ値でも TLB を全部捨てる (i386 に PCID は無い)。 */
static inline void arch_mmu_load_root(u32 root_phys)
{
    /* CR3 is a static TSS field: hardware task switch does not save it.
     * Keep the outgoing task root for the #DF diagnostic. */
    __asm__ volatile("mov %1, %0; mov %1, %%cr3"
                     : "=m"(kernel_tss.cr3) : "r"(root_phys) : "memory");
}

/* i386 互換: CR3 リロード方式。invlpg は i486+ なので使えない。 */
static inline void arch_mmu_flush_tlb(void)
{
    u32 cr3_val;
    __asm__ volatile("mov %%cr3, %0" : "=r"(cr3_val));
    __asm__ volatile("mov %0, %%cr3" : : "r"(cr3_val) : "memory");
}

static inline void arch_mmu_enable(void)
{
    u32 cr0_val;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0_val));
    cr0_val |= CR0_PG;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0_val) : "memory");
}

/* Restore all flags after probing EFLAGS.ID, including on pre-CPUID CPUs. */
static inline int arch_cpu_has_cpuid(void)
{
    u32 old, changed;
    __asm__ volatile("pushfl; popl %0" : "=r"(old));
    u32 probe = old ^ (1UL << 21);
    __asm__ volatile("pushl %0; popfl; pushfl; popl %1; pushl %2; popfl"
                     : "+r"(probe), "=&r"(changed) : "r"(old) : "cc", "memory");
    return ((old ^ changed) & (1UL << 21)) != 0;
}

static inline void arch_cpu_serialize(void)
{
    if (!arch_cpu_has_cpuid()) return;
    u32 a = 0, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d) : : "memory");
}
static inline int arch_cpu_has_mmx(void)
{
    if (!arch_cpu_has_cpuid()) return 0;
    u32 a = 0, b, c, d;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    if (a < 1) return 0;
    a = 1;
    __asm__ volatile("cpuid" : "+a"(a), "=b"(b), "=c"(c), "=d"(d));
    return (d & (1UL << 23)) != 0;
}

#endif /* ARCH_CPU_H */
