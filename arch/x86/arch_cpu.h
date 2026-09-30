/* ======================================================================== */
/*  arch/x86/arch_cpu.h — アドレス空間の切替と特権境界の x86 実装           */
/*                                                                          */
/*  契約は include/cpu.h にある。ここは i386 での実現方法だけを書く。         */
/*  このファイルを直接 #include してはいけない — 常に "cpu.h" を引くこと。    */
/*  ビルドは -Iarch/$(ARCH) で解決する (build/config.mk)。                   */
/*                                                                          */
/*  命令列は移設前 (kernel/paging.c・exec/exec.c のその場の asm) から         */
/*  1 文字も変えていない。移設の前後でカーネルの .o が md5 で一致することを   */
/*  確かめてある (docs/tasks/portability/ARM_GAUGE.md §9 順序 5)。           */
/* ======================================================================== */

#ifndef ARCH_CPU_H
#define ARCH_CPU_H

#include "gdt.h"        /* USER_CS / USER_DS (CONTRACTS C1 で凍結したセレクタ) */
#include "tss.h"        /* kernel_tss.esp0 — CPL=3 からの trap が使うスタック */

/* x86 制御レジスタのビット。CPU の持ち物なので機種ヘッダ (pc98.h) ではなく
 * ここに置く (順序 5 で include/pc98.h から移した)。 */
#define CR0_PE              0x00000001UL  /* プロテクトモード有効 */
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
    __asm__ volatile("mov %0, %%cr3" : : "r"(root_phys) : "memory");
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

/* ---- 特権境界 ---- */

/* iret で CPL=3 に降りる (M1d)。
 * CS=USER_CS(0x23) / SS=USER_DS(0x2B)。EFLAGS=0x202 (IF=1, IOPL=0)。
 * 記録先は TSS.ESP0: CPL=3 実行中の割り込み / int 0x80 のフレームが
 * この直下に積まれ、呼び手の setjmp フレームを踏まない。
 *
 * この cli は io.h の _disable() に分けられない。cli 〜 iret は
 * 「カーネル ESP の記録 → CR3 切替 → セグメント → フレーム積み → 特権降格」を
 * **一続きに** 行う必要があり、途中に割り込みが入ると TSS.ESP0 と実際の CR3 が
 * 食い違う。IF=1 になるのは iret が EFLAGS を積み直した瞬間だけで、カーネル側で
 * sti する窓は無い (docs/POLICY_DEBUG.md §4-19)。1 つの asm 文から出さないこと。 */
#define arch_enter_user(root_phys, user_sp, entry)                      \
do {                                                                    \
    __asm__ volatile(                                                   \
        "cli\n\t"                                                       \
        "movl %%esp, %[e0]\n\t"    /* TSS.ESP0 = 現在のカーネル ESP */  \
        "movl %[pd], %%cr3\n\t"    /* アプリ PD へ切替 */               \
        "movl %[uds], %%eax\n\t"                                        \
        "movw %%ax, %%ds\n\t"                                           \
        "movw %%ax, %%es\n\t"                                           \
        "movw %%ax, %%fs\n\t"                                           \
        "movw %%ax, %%gs\n\t"                                           \
        "pushl %[uds]\n\t"         /* SS = USER_DS */                   \
        "pushl %[uesp]\n\t"        /* ESP = ユーザスタック */           \
        "pushl $0x202\n\t"         /* EFLAGS: IF=1, IOPL=0 */           \
        "pushl %[ucs]\n\t"         /* CS = USER_CS */                   \
        "pushl %[eip]\n\t"         /* EIP = エントリポイント */         \
        "iret\n\t"                                                      \
        : [e0] "=m"(kernel_tss.esp0)                                    \
        : [pd]  "r"(root_phys),                                         \
          [uesp]"r"(user_sp),                                           \
          [eip] "r"(entry),                                             \
          [uds] "i"(USER_DS),                                           \
          [ucs] "i"(USER_CS)                                            \
        : "eax", "memory"                                               \
    );                                                                  \
} while (0)

/* CPL=0 のまま子のスタックへ切り替えて cdecl で呼ぶ。
 * 復帰ムーブは %esp/%ebp 相対では読めないので、退避先は呼び手の static。 */
#define arch_call_on_stack(saved_sp_slot, new_sp, entry)                \
do {                                                                    \
    __asm__ volatile(                                                   \
        "movl %%esp, %0\n\t"                                            \
        "movl %1, %%esp\n\t"                                            \
        "call *%2\n\t"                                                  \
        "movl %0, %%esp"                                                \
        : "=m"(saved_sp_slot)                                           \
        : "r"(new_sp), "r"(entry)                                       \
        : "eax", "ecx", "edx", "cc", "memory"                           \
    );                                                                  \
} while (0)

#endif /* ARCH_CPU_H */
