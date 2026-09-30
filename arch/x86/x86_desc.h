/* ======================================================================== */
/*  arch/x86/x86_desc.h — GDT / タスクレジスタのロード (x86 専用、契約なし)  */
/*                                                                          */
/*  GDT とセグメントの再ロード、TSS セレクタの ltr は x86 にしか無い仕組みで、 */
/*  他の CPU に「同じ意味」の原始命令が無い。だから include/ に契約を置かず、 */
/*  x86 専用のソース (kernel/gdt.c / kernel/tss.c) だけがこれを直接引く。     */
/*  -Iarch/$(ARCH) で解決するので、ARCH が x86 でなければ見つからずに止まる   */
/*  — それが「このソースは x86 専用」という印になる。                        */
/*                                                                          */
/*  名前が arch_*.h でないのは、arch_*.h が「include/ の契約の実装」を表す    */
/*  固定名だから (arch/README.md)。                                          */
/* ======================================================================== */

#ifndef X86_DESC_H
#define X86_DESC_H

#include "types.h"

/* GDT をロードし、CS を far jump で、DS/ES/FS/GS/SS を再ロードする。
 * `ptr` は limit(16) + base(32) の 6 バイトの GDT ポインタ。
 * セレクタ 0x08 / 0x10 は kernel/gdt.h の GDT_KERNEL_CS / GDT_KERNEL_DS
 * (CONTRACTS C1 で凍結)。命令列は移設前の kernel/gdt.c gdt_flush() のまま。 */
static inline void x86_load_gdt(u32 ptr)
{
    __asm__ volatile (
        "lgdt (%0)\n\t"
        "ljmp $0x08, $1f\n\t"
        "1:\n\t"
        "mov $0x10, %%ax\n\t"
        "mov %%ax, %%ds\n\t"
        "mov %%ax, %%es\n\t"
        "mov %%ax, %%fs\n\t"
        "mov %%ax, %%gs\n\t"
        "mov %%ax, %%ss\n\t"
        : : "r"(ptr) : "memory", "eax"
    );
}

/* タスクレジスタに TSS のセレクタをロードする。
 * 呼ぶ前に GDT の該当ディスクリプタが present であること。
 * `sel` は GDT_TSS_SEL (idx<<3 | RPL)。移設前 (kernel/tss.c) と同じく
 * "a" 制約で %eax に積んでから ltr %ax する。 */
static inline void x86_load_tr(unsigned int sel)
{
    __asm__ volatile ("ltr %%ax" : : "a"(sel));
}

#endif /* X86_DESC_H */
