/* ========================================================================= */
/*  RING3_GUARD — v2 M3 ハードニング: ヒープ/スタック境界ガードの検証        */
/*                 + GUI v1.1 K3: 共有ライブラリ帯域 .text の書き込み保護     */
/*                                                                           */
/*  ring3_hello/ring3_fault と同じ crt0/KAPI 非依存の自己完結バイナリ。      */
/*  引数で 2 つのケースを選ぶ (どちらも「書いたら kill される」ことの確認)。  */
/*                                                                           */
/*    ring3_guard          ケース A: ヒープ/スタック間のガードページ          */
/*                         (0x7BF000, 非present) へ書き込む                   */
/*    ring3_guard shlib    ケース B: 共有ライブラリ帯域の .text (0x400000)    */
/*                         へ書き込む。K3 でここは read-only + USER になった  */
/*                         ので保護違反 (#PF err bit0=1,bit1=1,bit2=1)。       */
/*                         ライブラリ未ロードでも USER が立っていないので     */
/*                         同じく #PF になる (どちらでも kill が正解)。       */
/*    ring3_guard cirrus   ケース C: Cirrus のリニア窓オフセット 0 = 表示面    */
/*                         (01000000h) へ書き込む。窓は master に supervisor   */
/*                         + PCD で張られ、アプリ PD で USER になるのは         */
/*                         クライアント面 (+4B000h) だけ (レビュー #5 ②)。     */
/*                         Cirrus 無しの機種では Not-Present。どちらも #PF。   */
/*    ring3_guard pegc     ケース D: PEGC のリニア窓 (F00000h) = 表示面へ      */
/*                         書き込む。同じく supervisor か Not-Present。       */
/*                         (9801 のプレーン VRAM A8000h は C2 で USER なので   */
/*                          対象外 — あちらはバックバッファが主記憶にある)     */
/*    ring3_guard bb       ケース E (**生き残るのが正解**): 9801 の主記憶      */
/*                         バックバッファ 6A000h へ書く。どのバックエンドが   */
/*                         選ばれていても exec が常に USER で写す (レビュー   */
/*                         #6: アプリの gfx_init でアクセラレータが失敗して   */
/*                         9801 へ落ちたときの描画先)。kill されたら退行。    */
/*                         0xA8000="BB??" かつ 0xA8004="SURV" で合格。          */
/*                                                                           */
/*  動作:                                                                     */
/*    1. テキスト VRAM に "RG"                                               */
/*    2. 0xA8000 に "GRD?" / "SLB?" マーカー (書込試行直前)                    */
/*    3. 対象へ書き込む → #PF → kill されるはず                              */
/*    4. 生き残ったら 0xA8004 に "SURV" を書いて sys_exit (ケース E 以外は     */
/*       =保護無効)                                                          */
/*                                                                           */
/*  PM 検証: fault_kill_count +1、0xA8000="GRD?"(または "SLB?") かつ          */
/*           0xA8004≠"SURV"、シリアルに [ring3] #PF ... addr=0x007BF000       */
/*           (ケース B は addr=0x00400000 と " [shlib band, WRITE]")、        */
/*           カーネル生存。                                                   */
/*                                                                           */
/*  引数は exec_run が CPL=3 のユーザスタックに call 互換で積む               */
/*  ([esp]=ダミー retaddr, [esp+4]=argc, ...) ので、cdecl の宣言でそのまま     */
/*  受け取れる。argv[1] の先頭 1 文字だけで分岐する (文字列はユーザスタック  */
/*  上にあるので CPL=3 から読める)。                                          */
/* ========================================================================= */

/* スロット番号だけの生成ヘッダ (型に依存しないので自己完結のまま include できる)。 */
#include "os32_kapi_slots.h"

/* crt0 を使わないので KAPI データ欄の配置の刻印を自分で置く (ヘッダ v3、票
 * TASK_KAPI_DATA_FIELDS)。mkos32x.py は刻印の無い ELF を断る。この試験は
 * データ欄を読まないが、exec は配置を照合するので v63 の値を持たせる。 */
OS32_KAPI_LAYOUT_STAMP();

void _start(int argc, char **argv) __attribute__((section(".text.startup"), used, noreturn));

void _start(int argc, char **argv)
{
    volatile unsigned short *tvram = (volatile unsigned short *)0x000A0000UL;
    volatile unsigned short *avram = (volatile unsigned short *)0x000A2000UL;
    volatile unsigned long  *mark  = (volatile unsigned long  *)0x000A8000UL;
    volatile unsigned int   *guard = (volatile unsigned int   *)0x007BF000UL;
    /* 共有ライブラリ帯域の先頭ページ = ジャンプ表 (.text/.rodata と同じ RO)。
     * include/memmap.h の MEM_SHLIB_BASE。カーネルヘッダは引けないので直値。 */
    volatile unsigned int   *shtext = (volatile unsigned int *)0x00400000UL;
    /* 表示面 (レビュー #5 ②)。Cirrus はグルーの lin_base (include/wab_xe10.h、
     * 01000000h)、PEGC は PEGC_LINEAR_BASE (F00000h)。直値なのは上と同じ理由。 */
    volatile unsigned int   *cirrus_vis = (volatile unsigned int *)0x01000000UL;
    volatile unsigned int   *pegc_vis   = (volatile unsigned int *)0x00F00000UL;
    /* 9801 の主記憶バックバッファ (include/memmap.h MEM_GFX_BB_BASE)。 */
    volatile unsigned int   *pc98_bb    = (volatile unsigned int *)0x0006A000UL;
    char sel = (argc > 1 && argv[1]) ? argv[1][0] : 0;

    tvram[76] = (unsigned short)'R'; avram[76] = 0x00E5;
    tvram[77] = (unsigned short)'G'; avram[77] = 0x00E5;

    if (sel == 'b') {
        /* ---- ケース E: 9801 主記憶バックバッファ (USER であるべき) ---- */
        /* 0x3F3F4242 = LE 42 42 3F 3F = "BB??" */
        mark[0] = 0x3F3F4242UL;
        *pc98_bb = 0x00000000UL;   /* 生き残れば下の "SURV" まで進む */
    } else if (sel == 'c') {
        /* ---- ケース C: Cirrus 表示面 (リニア窓オフセット 0) ---- */
        /* 0x3F534956 = LE 56 49 53 3F = "VIS?" */
        mark[0] = 0x3F534956UL;
        *cirrus_vis = 0xDEADBEEFUL;
    } else if (sel == 'p') {
        /* ---- ケース D: PEGC 表示面 (F00000h) ---- */
        /* 0x3F474550 = LE 50 45 47 3F = "PEG?" */
        mark[0] = 0x3F474550UL;
        *pegc_vis = 0xDEADBEEFUL;
    } else if (argc > 1) {
        /* ---- ケース B: 共有ライブラリ帯域 .text への書き込み (K3) ---- */
        /* 0x3F424C53 = LE 53 4C 42 3F = "SLB?" */
        mark[0] = 0x3F424C53UL;
        *shtext = 0xDEADBEEFUL;
    } else {
        /* ---- ケース A: ヒープ/スタック間のガードページ (v2 M3) ---- */
        /* 0x3F445247 = LE 47 52 44 3F = "GRD?" */
        mark[0] = 0x3F445247UL;
        *guard = 0xDEADBEEFUL;
    }

    /* ここに来た = 書けた。ケース E ではこれが正解、それ以外は保護が効いて
     * いない (退行)。0x56525553 = LE "SURV" */
    mark[1] = 0x56525553UL;

    /* sys_exit(0) を KAPI トランポリンと同じ規約で呼ぶ: eax = スロット、引数は
     * ユーザスタックの [esp+4] から (先頭 1 語はスタブの戻り番地ぶん)。
     * スロット番号は生成ヘッダ os32_kapi_slots.h の KAPI_SLOT_SYS_EXIT (= 84、
     * 末尾追記のみなので不変)。かつては eax=0 で呼んでいたが、それはスロット
     * 0 = gfx_init であって終了しない (2026-09-06 実測: ケース E で GFX モードに
     * 入ったまま無限ループ、CTRL+STOP で回収した)。 */
    __asm__ __volatile__("pushl $0\n\tpushl $0\n\tint $0x80"
                         : : "a"(KAPI_SLOT_SYS_EXIT) : "memory");

    for (;;) {
        __asm__ __volatile__("" ::: "memory");
    }
}
