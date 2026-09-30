/* ========================================================================= */
/*  RING3_FAULT — v2 M1e の CPL=3 フォールト kill 検証用 (KAPI 非依存)        */
/*                                                                           */
/*  【重要】ring3_hello と同じく crt0 も KAPI も使わない自己完結バイナリ。    */
/*  M1 (M2 トランポリン前) は CPL=3 からカーネルコードページを呼べないので、  */
/*  KAPI を使う userland/tests/faultprobe/ は M1e では使えない。こちらは      */
/*  独自 _start + int 0x80 終了で、KAPI を一切呼ばずに「わざと不正アクセス」   */
/*  だけを行う。                                                              */
/*                                                                           */
/*  動作:                                                                     */
/*    1. テキスト VRAM に "RF" (CPL=3 到達の可視痕跡)                        */
/*    2. 0xA8000 に "FLT?" マーカー (フォールト試行の直前まで来た証拠)        */
/*    3. カーネル帯域 0x100000 へ書き込む → CPL=3 では PTE=supervisor なので   */
/*       #PF。M1e の #PF ハンドラがアプリを kill し fault_kill_count を        */
/*       インクリメントしてシェルへ戻す。ここから先へは進まないはず。         */
/*    4. もし kill されず生き残ったら 0xA8004 に "SURV" を書いて int 0x80 で   */
/*       終了する = 保護が効いていない ([ABI4] の穴が残っている) 証拠。        */
/*                                                                           */
/*  PM 検証 (os32-cycle fault-test):                                          */
/*    - fault_kill_count が +1 される                                        */
/*    - 0xA8000 = "FLT?" かつ 0xA8004 ≠ "SURV" (kill 済み)                   */
/*    - カーネル生存 (ver 応答) + シェル復帰                                  */
/* ========================================================================= */

#include "os32_kapi_slots.h"

/* crt0 を使わないので KAPI データ欄の配置の刻印を自分で置く (ヘッダ v3、票
 * TASK_KAPI_DATA_FIELDS)。mkos32x.py は刻印の無い ELF を断る。この試験は
 * データ欄を読まないが、exec は配置を照合するので v63 の値を持たせる。 */
OS32_KAPI_LAYOUT_STAMP();

/* app.ld の ENTRY(_start)。crt0 を link しないのでこれがエントリになる。 */
void _start(void) __attribute__((section(".text.startup"), used, noreturn));

void _start(void)
{
    volatile unsigned short *tvram = (volatile unsigned short *)0x000A0000UL;
    volatile unsigned short *avram = (volatile unsigned short *)0x000A2000UL;
    volatile unsigned long  *mark  = (volatile unsigned long  *)0x000A8000UL;
    volatile unsigned int   *kern  = (volatile unsigned int   *)0x00100000UL;

    /* 1. 画面右上 (行0, 桁76-77) に "RF" */
    tvram[76] = (unsigned short)'R'; avram[76] = 0x00E4;
    tvram[77] = (unsigned short)'F'; avram[77] = 0x00E4;

    /* 2. フォールト試行の直前マーカー。0x3F544C46 = LE 46 4C 54 3F = "FLT?" */
    mark[0] = 0x3F544C46UL;

    /* 3. カーネル帯域へ書き込み。CPL=3 では #PF して kill されるはず。
     *    ここで戻ってこない (M1e で保護が効いていれば)。 */
    *kern = 0xDEADBEEFUL;

    /* 4. ここに来た = kill されなかった = 保護が効いていない。
     *    0x56525553 = LE 53 55 52 56 = "SURV" */
    mark[1] = 0x56525553UL;

    /* sys_exit(1) を KAPI トランポリンと同じ規約で呼ぶ: eax = スロット、
     * 引数はユーザスタックの [esp+4] から (先頭 1 語はスタブの戻り番地ぶん)。
     * スロット番号は生成ヘッダ os32_kapi_slots.h の KAPI_SLOT_SYS_EXIT (= 84、
     * 末尾追記のみなので不変)。かつては eax=0 / ebx=status で呼んでいたが、
     * それはスロット 0 = gfx_init であって終了しない (2026-09-06 に
     * ring3_guard.c で実測・修正済み。この 2 本が修正から漏れていた)。
     * 先に積んだ 1 語が [esp+4] = 引数。status=1 で「生き残ってしまった」。 */
    __asm__ __volatile__("pushl $1\n\tpushl $0\n\tint $0x80"
                         : : "a"(KAPI_SLOT_SYS_EXIT) : "memory");

    for (;;) {
        __asm__ __volatile__("" ::: "memory");
    }
}
