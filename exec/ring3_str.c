/* ======================================================================== */
/*  RING3_STR.C — KAPI が CPL=3 へ返す文字列の置き場 (票 T9 §12 R1)         */
/*                                                                          */
/*  設計と番地の根拠は exec/ring3_str.h の冒頭。ここに置かないもの:          */
/*    - トランポリンページを作る / 張る手順 → exec/exec.c                    */
/*    - 「いま CPL=3 のディスパッチ中か」の判定 → exec/exec.c の             */
/*      ring3_in_syscall (呼び出し側が値で渡す)                              */
/*  だからこの .c はホストでそのまま試験できる                               */
/*  (tools/tests/ring3_str_host.c)。                                        */
/* ======================================================================== */

#include "ring3_str.h"
#include "kstring.h"
#include "paging.h"     /* PTE_PRESENT / PTE_RW / PTE_USER ([C4]: ビットは正典から) */

const char *ring3_user_str(int in_syscall, char *scratch, u32 cap,
                           const char *src)
{
    /* CPL=0 の呼び手 (常駐シェル / カーネル自身) はカーネル帯をそのまま
     * 読めるので写さない — 写すと「CUI で cd した直後の pwd」まで
     * トランポリンページ経由になり、exec_init より前の呼び出しで落ちる。 */
    if (!in_syscall) return src;
    if (scratch == 0 || cap == 0) return src;   /* 写し場が無い (起動途中) */
    if (src == 0) return src;
    /* kstrncpy の n は**バッファ全体サイズ**なので、cap ちょうどを渡せば
     * 必ず NUL 終端する (cap - 1 文字まで写る)。 */
    kstrncpy(scratch, src, cap);
    return (const char *)scratch;
}

int ring3_pte_writable_ok(u32 pte_flags)
{
    u32 need = (u32)(PTE_PRESENT | PTE_RW | PTE_USER);
    return ((pte_flags & need) == need) ? 1 : 0;
}

int ring3_pde_walkable_ok(u32 pde_flags)
{
    u32 need = (u32)(PTE_PRESENT | PTE_RW | PTE_USER);
    if (pde_flags & (u32)PTE_PS) return 0;      /* 4MB ページ: 下に PT が無い */
    /* **PDE の RW / USER も見る** — i386 の実効権限は PDE と PTE の論理積
     * なので、PTE が RW + USER でも PDE が supervisor / RO ならアプリは
     * 書けない (Approve 後の注意 4)。 */
    return ((pde_flags & need) == need) ? 1 : 0;
}

int ring3_range_overlaps(u32 p, u32 len, u32 base, u32 end)
{
    u32 last;

    if (len == 0) return 0;
    if (end <= base) return 0;              /* 空の帯 (未ロードの shlib 等) */
    if (p + len < p) return 1;              /* **桁あふれは重なり扱い** (安全側) */
    last = p + len - 1u;
    if (last < base) return 0;
    if (p >= end) return 0;
    return 1;
}

int ring3_guard_active(int in_syscall, int wm_depth)
{
    /* ディスパッチャの外 (CPL=0 の直呼び) は対象外。ディスパッチの中でも、
     * カーネルが WM のコードへ入っているあいだは常駐側の呼び出しとして扱う
     * (根拠は ring3_str.h の同名の節)。負の深さは壊れた状態 — 安全側に
     * 「ガードを効かせる」。 */
    if (!in_syscall) return 0;
    if (wm_depth > 0) return 0;
    return 1;
}
