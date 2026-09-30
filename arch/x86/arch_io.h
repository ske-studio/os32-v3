/* ======================================================================== */
/*  arch/x86/arch_io.h — CPU 原始命令の x86 実装                             */
/*                                                                          */
/*  契約は include/io.h にある。ここは i386 での実現方法だけを書く。          */
/*  このファイルを直接 #include してはいけない — 常に "io.h" を引くこと。     */
/*  ビルドは -Iarch/$(ARCH) で解決する (build/config.mk)。                   */
/*                                                                          */
/*  対象: 割り込みの許可/禁止、その保存/復元、CPU 停止、IDT のロード。        */
/*  ポート I/O は CPU ではなく機種の持ち物なので platform/$(PLATFORM)/ 側。  */
/* ======================================================================== */

#ifndef ARCH_IO_H
#define ARCH_IO_H

/* ---- 割り込み制御 ---- */
static inline void _enable(void) {
    __asm__ volatile("sti" : : : "memory");
}

static inline void _disable(void) {
    __asm__ volatile("cli" : : : "memory");
}

/* ---- 割り込み状態の保存/復元 ---- */
static inline unsigned int irq_save(void) {
    unsigned int flags;
    __asm__ volatile("pushfl\n\tpopl %0\n\tcli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(unsigned int flags) {
    __asm__ volatile("pushl %0\n\tpopfl" : : "r"(flags) : "memory", "cc");
}

/* EFLAGS の IF (bit 9) を**読むだけ**。cli も sti もしない。
 * irq_save() と違って状態を変えないので、「いま hlt してよいか」の
 * 判定に使える (IF=0 の hlt は二度と起きない)。 */
#define X86_EFLAGS_IF  0x00000200U
static inline int _irq_enabled(void) {
    unsigned int flags;
    __asm__ volatile("pushfl\n\tpopl %0" : "=r"(flags) : : "memory");
    return (flags & X86_EFLAGS_IF) ? 1 : 0;
}

/* ---- 特権命令 ---- */
static inline void _lidt(void *ptr) {
    __asm__ volatile("lidt (%0)" : : "r"(ptr) : "memory");
}

/* ---- CPU 停止 ---- */

static inline void _halt(void) {
    __asm__ volatile("hlt" : : : "memory");
}

/* x86 の sti は直後の 1 命令のあいだ割り込みを遅らせるので、"sti\n\thlt" は
 * 1 つの asm 文に収めてあるかぎり不可分。**分けてはいけない** (契約は
 * include/io.h の _idle() の註)。 */
static inline void _idle(void) {
    __asm__ volatile("sti\n\thlt" : : : "memory");
}

static inline void _stop(void) {
    __asm__ volatile("cli\n\thlt" : : : "memory");
}

#endif /* ARCH_IO_H */
