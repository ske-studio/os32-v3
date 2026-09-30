/* ======================================================================== */
/*  TSS.C — タスクステートセグメントと I/O 許可ビットマップ                 */
/* ======================================================================== */

#include "tss.h"
#include "kstring.h"
#include "x86_desc.h"   /* x86_load_tr (arch/x86/、契約なし) */

/* I/O 許可ビットマップは全 65536 ポートを覆う必要がある (1 ポート 1 bit)。
 * 足りないとリミット外のポートが CPL/IOPL 次第で素通りする。 */
STATIC_ASSERT(TSS_IOMAP_SIZE == 65536 / 8, tss_iomap_covers_all_ports);

/* kernel/v86_entry.asm が TSS.ESP0 を `mov [kernel_tss + 4], eax` と
 * オフセット直書きで更新している (NASM から C の構造体は見えない)。
 * ここがずれると V86 の #GP が拾えないスタックにフレームを積み、
 * 原因不明のトリプルフォルトになる。構造体を触ったらここで止まる。 */
STATIC_ASSERT((u32)(&((struct tss_entry *)0)->esp0) == 4, tss_esp0_at_offset_4);

/* GDT 側で TSS ディスクリプタを作るため公開する */
struct tss_entry kernel_tss;

/* gdt.c が TSS ディスクリプタを設定してから ltr する */
extern void gdt_set_tss(u32 base, u32 limit);

#define TSS_SELECTOR    0x18    /* GDT エントリ 3 */
#define KERNEL_DS       0x10

void tss_init(u32 kernel_esp0)
{
    u32 base;
    u32 limit;

    kmemset(&kernel_tss, 0, sizeof(kernel_tss));

    kernel_tss.ss0  = KERNEL_DS;
    kernel_tss.esp0 = kernel_esp0;

    /* iomap_base は TSS 先頭からのオフセット。ディスクリプタのリミットが
     * ここより手前で切れていると、CPU は「ビットマップ無し = 全ポート禁止」
     * と解釈する。逆にリミットを伸ばしておけばビットマップが有効になる。 */
    kernel_tss.iomap_base = (u16)((u8 *)&kernel_tss.iomap - (u8 *)&kernel_tss);

    /* 既定は全ポート #GP。素通しにするポートは V86 セッション開始時に
     * 明示的に開ける (docs/tasks/v86v2/00_approach_study.md §4.1)。 */
    tss_iomap_deny_all();

    /* 番兵。CPU はビットマップの最終バイトを跨いで読むことがある。 */
    kernel_tss.iomap[TSS_IOMAP_SIZE] = 0xFF;

    base  = (u32)&kernel_tss;
    limit = sizeof(kernel_tss) - 1;
    gdt_set_tss(base, limit);

    x86_load_tr(TSS_SELECTOR);
}

void tss_set_esp0(u32 esp0)
{
    kernel_tss.esp0 = esp0;
}

u32 tss_get_esp0(void)
{
    return kernel_tss.esp0;
}

void tss_iomap_allow(u16 port)
{
    kernel_tss.iomap[port >> 3] &= (u8)~(1 << (port & 7));
}

void tss_iomap_deny(u16 port)
{
    kernel_tss.iomap[port >> 3] |= (u8)(1 << (port & 7));
}

void tss_iomap_allow_range(u16 start, u16 end)
{
    u32 p;
    for (p = start; p <= (u32)end; p++) {
        tss_iomap_allow((u16)p);
    }
}

void tss_iomap_deny_range(u16 start, u16 end)
{
    u32 p;
    for (p = start; p <= (u32)end; p++) {
        tss_iomap_deny((u16)p);
    }
}

void tss_iomap_deny_all(void)
{
    kmemset(kernel_tss.iomap, 0xFF, TSS_IOMAP_SIZE);
    kernel_tss.iomap[TSS_IOMAP_SIZE] = 0xFF;
}

int tss_iomap_is_allowed(u16 port)
{
    return (kernel_tss.iomap[port >> 3] & (1 << (port & 7))) ? 0 : 1;
}
