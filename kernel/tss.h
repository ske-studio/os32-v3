/* ======================================================================== */
/*  TSS.H — タスクステートセグメントと I/O 許可ビットマップ                 */
/*                                                                          */
/*  OS32 はタスクスイッチに TSS を使わない (シングルタスク + setjmp/longjmp) */
/*  が、V86 モードには TSS が二重に必須になる。                             */
/*                                                                          */
/*   1. V86 ゲスト (CPL=3) から #GP でカーネルに戻るとき、CPU は TSS の      */
/*      SS0:ESP0 からカーネルスタックを取る                                 */
/*   2. V86 の IN/OUT を「どのポートはトラップし、どのポートは素通しか」で   */
/*      分けられるのは I/O 許可ビットマップだけ                             */
/*                                                                          */
/*  2 が性能の要。docs/tasks/v86v2/03_ys_profile.md の実測では、ゲストの     */
/*  I/O の 7〜8 割が FM 音源のステータスポーリングだった。ここを #GP で      */
/*  拾うと 386DX 20MHz では成立しないが、ビットマップで素通しにすれば       */
/*  コストはゼロになる。                                                    */
/* ======================================================================== */

#ifndef __TSS_H
#define __TSS_H

#include "types.h"

/* I/O 許可ビットマップは 65536 ポート ÷ 8 = 8192 バイト。
 * 末尾に 0xFF の番兵が 1 バイト要る (CPU が最終バイトを跨いで読むため)。 */
#define TSS_IOMAP_SIZE  8192

/* i386 の TSS (104 バイト) + I/O 許可ビットマップ */
struct tss_entry {
    u32 prev_tss;
    u32 esp0;           /* Ring0 スタックポインタ (V86 → #GP でここに切替) */
    u32 ss0;            /* Ring0 スタックセグメント */
    u32 esp1;
    u32 ss1;
    u32 esp2;
    u32 ss2;
    u32 cr3;
    u32 eip;
    u32 eflags;
    u32 eax, ecx, edx, ebx;
    u32 esp, ebp, esi, edi;
    u32 es, cs, ss, ds, fs, gs;
    u32 ldt;
    u16 trap;
    u16 iomap_base;     /* TSS 先頭からビットマップまでのオフセット */
    u8  iomap[TSS_IOMAP_SIZE + 1];
} __attribute__((packed));

/* カーネルの TSS 本体 (定義は kernel/tss.c)。CPL=3 へ降りる arch/x86 の
 * 原始命令 (arch_enter_user) が ESP0 を書くのでここで公開する。 */
extern struct tss_entry kernel_tss;

/* ======== API ======== */

/* TSS を初期化して ltr でロードする。gdt_init() の後に呼ぶこと。
 * kernel_esp0 には V86 から戻るときに使うカーネルスタックの頂点を渡す。 */
void tss_init(u32 kernel_esp0);

/* Ring0 スタックポインタの差し替えと取得。
 * V86 セッションの出入りで専用スタックに切り替えるために使う。 */
void tss_set_esp0(u32 esp0);
u32  tss_get_esp0(void);

/* I/O 許可ビットマップ。bit=0 で素通し、bit=1 で #GP。
 * 既定は全ポート拒否 (tss_init 直後の状態)。 */
void tss_iomap_allow(u16 port);
void tss_iomap_deny(u16 port);
void tss_iomap_allow_range(u16 start, u16 end);
void tss_iomap_deny_range(u16 start, u16 end);
void tss_iomap_deny_all(void);

/* 指定ポートが素通し設定かどうか (デバッグ・検証用) */
int  tss_iomap_is_allowed(u16 port);

#endif /* __TSS_H */
