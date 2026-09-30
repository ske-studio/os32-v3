/* ========================================================================= */
/*  RING3_HELLO — v2 M1 の CPL=3 (リング3) 検証用 最小自己完結プログラム     */
/*                                                                           */
/*  【重要】このプログラムは crt0 も KAPI も一切使わない自己完結バイナリ。   */
/*  標準 crt0 (_start_c) は kapi->sys_exit() 等カーネル関数ポインタを呼ぶが、 */
/*  M1 の時点では KAPI トランポリン (M2) が無いため CPL=3 からカーネルの      */
/*  コードページ (USER でない) を実行すると即 #PF/#GP する。よって独自 _start */
/*  を持ち、終了は int 0x80 (sys_exit) で行う。                               */
/*                                                                           */
/*  ビルド (PM が programs.mk に専用ルールを追加): crt0 を link せず、        */
/*  mkos32x.py に --ring3 を付けて OS32X_FLAG_RING3 を立てる。詳細は          */
/*  コーダー1 の M1 報告を参照。                                              */
/*                                                                           */
/*  動作:                                                                     */
/*    1. テキスト VRAM に "R3" を表示 (CPL=3 到達の可視痕跡, V2)             */
/*    2. グラフィック VRAM 0xA8000 に機械可読マーカー 'RNG3' を書く (V2)      */
/*    3. しばらく CPL=3 で spin (タイマ/KBD 割り込みが入るか= V3)            */
/*    4. 0xA8004 に 'DONE' を書いてから int 0x80 で終了                       */
/*                                                                           */
/*  CPL=3 が触れるのは USER マップされた領域だけ: 0x400000 帯 / ユーザ        */
/*  スタック / VRAM / SHM。カーネル帯域 (0x100000-) に書こうとすると #PF に    */
/*  なる (M1e で kill されるが M1 の正常系では触らない)。                     */
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
    unsigned long i;

    /* 1. 画面右上 (行0, 桁76-77) に "R3" */
    tvram[76] = (unsigned short)'R'; avram[76] = 0x00E1;
    tvram[77] = (unsigned short)'3'; avram[77] = 0x00E1;

    /* 2. グラフィック VRAM に機械可読マーカー。
     *    0x33474E52 は LE で 52 4E 47 33 = "RNG3"。 */
    mark[0] = 0x33474E52UL;

    /* 3. CPL=3 で spin。割り込みが壊れていればハングし、生きていれば
     *    この間もカーネルの tick_count が進む (PM が emu_read_mem で確認)。 */
    for (i = 0; i < 20000000UL; i++) {
        __asm__ __volatile__("" ::: "memory");
    }

    /* 4. 完了マーカー 0x454E4F44 = LE 44 4F 4E 45 = "DONE" */
    mark[1] = 0x454E4F44UL;

    /* sys_exit(0) を KAPI トランポリンと同じ規約で呼ぶ: eax = スロット、
     * 引数はユーザスタックの [esp+4] から (先頭 1 語はスタブの戻り番地ぶん)。
     * スロット番号は生成ヘッダ os32_kapi_slots.h の KAPI_SLOT_SYS_EXIT (= 84、
     * 末尾追記のみなので不変)。かつては eax=0 / ebx=status で呼んでいたが、
     * それはスロット 0 = gfx_init であって終了しない (2026-09-06 に
     * ring3_guard.c で実測・修正済み。この 2 本が修正から漏れていた)。 */
    __asm__ __volatile__("pushl $0\n\tpushl $0\n\tint $0x80"
                         : : "a"(KAPI_SLOT_SYS_EXIT) : "memory");

    for (;;) {
        __asm__ __volatile__("" ::: "memory");
    }
}
