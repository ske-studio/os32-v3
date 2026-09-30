/* ======================================================================== */
/*  FAULTPROBE — リング3 フォールト注入テスト (M3 検証)                      */
/*                                                                          */
/*  CPL=3 でわざと不正アクセスを行い、「アプリだけが死に、カーネルは生存し、  */
/*  シェルに戻る」ことを確認するためのプログラム。制約 [ABI4] 解消の実証。    */
/*                                                                          */
/*  使い方 (ホスト側 os32-cycle fault-test が各ケースを順に実行):            */
/*    faultprobe <case>                                                     */
/*      1  カーネル帯域 (0x100000) へ書き込み    → ring3 で #PF kill         */
/*      2  未マップ番地 (0x7000000) を読む        → #PF kill                 */
/*      3  SQLite 帯域 (0x200000) へ書き込み       → ring3 で #PF kill        */
/*      4  KAPI にワイルドポインタを渡す           → M2 ディスパッチャが弾く  */
/*                                                                          */
/*  各ケースが #PF で kill されれば、この関数は戻ってこない。カーネルは       */
/*  fault_kill_count を増やしてシェルへ戻す (カーネル側 M1e/M3 実装)。        */
/*  もし戻ってきたら = 保護が効いていない = 失敗。その旨を出して exit(1)。    */
/*                                                                          */
/*  注意: このプログラム自身は特権命令を含まない (不正アクセスは volatile C   */
/*  のみ)。check_privileged.py を通ること。                                  */
/*                                                                          */
/*  現状 (リング3 未導入・CPL=0) では:                                        */
/*    - case 2 は未マップ読みなので CPL=0 でも #PF する (paging 有効)         */
/*    - case 1/3 はカーネル帯域書き込みが CPL=0 では通ってしまう ([ABI4])。   */
/*      これがまさにリング3 で塞ぐ穴。M1e で「kill される」に変わる           */
/* ======================================================================== */

#include "os32api.h"

/* 補助関数は main の後ろに置く (crt0 の先頭ジャンプ規約)。前方宣言のみ前置。 */
static int my_atoi(const char *s);

/* main() はソース中の最初の関数でなければならない (crt0 が先頭へ飛ぶ) */
void main(int argc, char **argv, KernelAPI *api)
{
    int n;
    volatile unsigned int *p;
    unsigned int sink;

    n = (argc >= 2) ? my_atoi(argv[1]) : 1;

    api->kprintf(0x0E, "faultprobe: case %d start\n", n);

    switch (n) {
    case 1: /* カーネル帯域へ書き込み */
        p = (volatile unsigned int *)0x100000UL;
        *p = 0xDEADBEEF;
        break;
    case 2: /* 未マップ番地を読む */
        p = (volatile unsigned int *)0x7000000UL;
        sink = *p;
        (void)sink;
        break;
    case 3: /* SQLite 帯域へ書き込み */
        p = (volatile unsigned int *)0x200000UL;
        *p = 0xDEADBEEF;
        break;
    case 4: /* KAPI にワイルドポインタ (M2 ディスパッチャが範囲検証で弾く) */
        api->sys_unlink((const char *)0xDEADBEEFUL);
        break;
    default:
        api->kprintf(0x04, "faultprobe: unknown case %d\n", n);
        api->sys_exit(2);
    }

    /* ここに来た = kill されなかった = 保護が効いていない */
    api->kprintf(0x04, "faultprobe: CASE %d SURVIVED (protection failed)\n", n);
    api->sys_exit(1);
}

/* main() の後ろに補助関数を置く (crt0 の先頭ジャンプ規約) */
static int my_atoi(const char *s)
{
    int v = 0;
    if (!s) return 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (*s - '0');
        s++;
    }
    return v;
}
