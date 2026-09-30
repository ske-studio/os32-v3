/* ======================================================================== */
/*  tools/tests/host_arch/arch_cpu.h — include/cpu.h の契約のホスト実装      */
/*                                                                          */
/*  ホスト試験 (gcc -m32、CPL=3 の Linux プロセス) は CR0 / CR3 を触れない。 */
/*  そこで -I をこのディレクトリに向け、arch/x86/arch_cpu.h の代わりに       */
/*  これを引かせる (契約ヘッダ include/cpu.h は本物のまま)。実物のソースを    */
/*  一字も書き換えずにホストで走らせるための差し替え口。                     */
/*                                                                          */
/*  順序 5 より前は各試験スクリプトが kernel/paging.c の asm 行を文字列置換   */
/*  していた。asm が arch/x86/arch_cpu.h へ移ったので、置換ではなく          */
/*  **契約の別実装**に替えてある (置換は対象が消えると黙って効かなくなる)。   */
/*                                                                          */
/*  使う側の約束: `host_cr3` を先に宣言しておくこと (試験ハーネスの先頭)。    */
/*  変換表の根はそこに置き、CR3 の代わりに読み書きする。                     */
/* ======================================================================== */

#ifndef ARCH_CPU_H
#define ARCH_CPU_H

static inline u32 arch_mmu_current_root(void)
{
    return host_cr3;
}

static inline void arch_mmu_load_root(u32 root_phys)
{
    host_cr3 = root_phys;
}

/* 実機では CR3 の載せ直し。ホストには TLB が無いので何もしない。 */
static inline void arch_mmu_flush_tlb(void)
{
}

/* 実機では CR0.PG を立てる。ホストでは観測点が無いので何もしない。 */
static inline void arch_mmu_enable(void)
{
}

/* 特権境界はホストで再現できない。呼ばれたらその場で止める
 * (ホスト試験に載っているのは kernel/paging.c だけで、呼び手はいない)。 */
#define arch_enter_user(root_phys, user_sp, entry)                      \
do {                                                                    \
    (void)(root_phys); (void)(user_sp); (void)(entry);                  \
    for (;;) { }                                                        \
} while (0)

#define arch_call_on_stack(saved_sp_slot, new_sp, entry)                \
do {                                                                    \
    (void)(new_sp); (void)(entry);                                      \
    (saved_sp_slot) = 0;                                                \
    for (;;) { }                                                        \
} while (0)

#endif /* ARCH_CPU_H */
