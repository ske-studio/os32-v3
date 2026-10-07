/* PM observation / per-case expectations: ring3_marker.h acceptance table. */
#include "ring3_marker.h"
#include "memmap.h"
#include "os32api.h"

/* crt0 を使わないので KAPI データ欄の配置の刻印を自分で置く (ヘッダ v3、票
 * TASK_KAPI_DATA_FIELDS)。mkos32x.py は刻印の無い ELF を断る。この試験は
 * データ欄を読まないが、exec は配置を照合するので v63 の値を持たせる。 */
OS32_KAPI_LAYOUT_STAMP();

void _start(int argc, char **argv) __attribute__((section(".text.startup"), used, noreturn));

void _start(int argc, char **argv)
{
    volatile unsigned long *mark = r3_marker(0x4752UL); /* RG */
    volatile unsigned int   *guard = (volatile unsigned int   *)(MEM_APP_STACK_TOP - MEM_EXEC_STACK_SIZE - MEM_GUARD_SIZE);
    /* 共有ライブラリ帯域の先頭ページ = ジャンプ表 (.text/.rodata と同じ RO)。
     * include/memmap.h の MEM_SHLIB_BASE。memmap.h の正典定数を使用。 */
    volatile unsigned int   *shtext = (volatile unsigned int *)MEM_SHLIB_BASE;
    /* 表示面 (レビュー #5 ②)。Cirrus はグルーの lin_base (include/wab_xe10.h、
     * 01000000h)、PEGC は PEGC_LINEAR_BASE (F00000h)。直値なのは上と同じ理由。 */
    volatile unsigned int   *cirrus_vis = (volatile unsigned int *)0x01000000UL;
    volatile unsigned int   *pegc_vis   = (volatile unsigned int *)0x00F00000UL;
    /* 9801 の主記憶バックバッファ (include/memmap.h MEM_GFX_BB_BASE)。 */
    volatile unsigned int   *pc98_bb    = (volatile unsigned int *)MEM_GFX_BB_BASE;
    char sel = (argc > 1 && argv[1]) ? argv[1][0] : 0;

    if (sel == 'l' || sel == 'f') {
        /* F: legitimate CLIENT survives, the same VA faults after shutdown. */
        GFX_Framebuffer fb = {0};
        (void)r3_call(KAPI_SLOT_GFX_INIT, 0);
        (void)r3_call(KAPI_SLOT_GFX_GET_FRAMEBUFFER, (unsigned long)&fb);
        if (!fb.planes[0]) r3_exit(1);
        volatile unsigned char *client = fb.planes[0];
        r3_arm(mark, 0x3F53454CUL, (unsigned long)client); /* LES? */
        *client = 0;
        mark[1] = R3_SURV;
        (void)r3_call(KAPI_SLOT_GFX_SHUTDOWN, 0);
        if (sel == 'f') {
            mark[1] = 0;
            r3_arm(mark, 0x3F564552UL, (unsigned long)client); /* REV? */
            *client = 0;
        }
    } else if (sel == 'b') {
        /* ---- ケース E: 9801 主記憶バックバッファ (USER であるべき) ---- */
        /* 0x3F3F4242 = LE 42 42 3F 3F = "BB??" */
        r3_arm(mark, 0x3F3F4242UL, (unsigned long)pc98_bb);
        *pc98_bb = 0x00000000UL;   /* 生き残れば下の "SURV" まで進む */
    } else if (sel == 'c') {
        /* ---- ケース C: Cirrus 表示面 (リニア窓オフセット 0) ---- */
        /* 0x3F534956 = LE 56 49 53 3F = "VIS?" */
        r3_arm(mark, 0x3F534956UL, (unsigned long)cirrus_vis);
        *cirrus_vis = 0xDEADBEEFUL;
    } else if (sel == 'p') {
        /* ---- ケース D: PEGC 表示面 (F00000h) ---- */
        /* 0x3F474550 = LE 50 45 47 3F = "PEG?" */
        r3_arm(mark, 0x3F474550UL, (unsigned long)pegc_vis);
        *pegc_vis = 0xDEADBEEFUL;
    } else if (argc > 1) {
        /* ---- ケース B: 共有ライブラリ帯域 .text への書き込み (K3) ---- */
        /* 0x3F424C53 = LE 53 4C 42 3F = "SLB?" */
        r3_arm(mark, 0x3F424C53UL, (unsigned long)shtext);
        *shtext = 0xDEADBEEFUL;
    } else {
        /* ---- ケース A: ヒープ/スタック間のガードページ (v2 M3) ---- */
        /* 0x3F445247 = LE 47 52 44 3F = "GRD?" */
        r3_arm(mark, 0x3F445247UL, (unsigned long)guard);
        *guard = 0xDEADBEEFUL;
    }

    /* ここに来た = 書けた。ケース E ではこれが正解、それ以外は保護が効いて
     * いない (退行)。0x56525553 = LE "SURV" */
    mark[1] = R3_SURV;

    r3_exit(0);
}
