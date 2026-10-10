/* ======================================================================== */
/*  KOUT_TEST.C — 出力ポインタの書き込み可検査の CPL=3 受入 (NULL 非零長は kill)       */
/*                                                                          */
/*  票: docs/archive/kernel_v21/TASK_KAPI_OUTPUT_GUARD.md 受入 G2 / G4            */
/*                                                                          */
/*  生成される KAPI ラッパは、出力引数に渡された範囲が present + RW + USER   */
/*  であることを `ring3_user_ranges_writable` で確かめてから target を呼ぶ。  */
/*  検査は `ring3_in_syscall` が立っているときだけ働くので、**CPL=0 からは    */
/*  1 行も走らない** — kselftest では踏めず、int 0x80 を通った本物の呼び      */
/*  出しはここでしか作れない (time_test と同じ理由)。                        */
/*                                                                          */
/*  見るもの (代表 6 本、A 型 2 + B 型 4):                                   */
/*    sys_read / np2_get_version   長さ引数つきの出力バッファ                */
/*    rtc_read / console_get_size / pci_get / ide_read_sector  固定長        */
/*                                                                          */
/*  各本について                                                            */
/*    (a) **正常系** — アプリのスタックとヒープ (どちらも非恒等写像) へ      */
/*        これまでどおり書けること。**回帰の本体はここ** — 検査が            */
/*        厳しすぎると普通のアプリが黙って死ぬ。                            */
/*    (b) **エラー系** — 有効な出力バッファで、不正 fd / index / 不在装置。 */
/*        NULL + 非零長は target より前に kill。ホストの実 wrap 試験で確認。 */
/*                                                                          */
/*  ⚠ ここで作れないもの (PM が NP21/W で手で見る。票 §3 の G2):             */
/*    **読み取り専用の USER ページ (共有ライブラリの `.text`) を出力に渡す**  */
/*    → kill。アプリ自身の `.text` はアプリ帯なので RW で写っており、        */
/*    渡しても検査は通って**自分のコードが壊れる**だけ。shlib の `.text` の   */
/*    番地はそれを attach した GUI アプリでないと持てない。安全に作れない    */
/*    ので、ここでは 1 度も試さない ([V4]: 見ていないものは見ていないと書く)。*/
/*    NULL + 非零長の kill はこの単一プロセスの試験では実行しない。         */
/*    tools/tests/test_kapi_ranges.py が実 wrap + B1 で拒否を確認する。        */
/* ======================================================================== */

#include "os32api.h"
#include <stdlib.h>          /* newlib malloc — アプリのヒープ (非恒等写像) */

#define KOUT_PATH     "/etc/profile"
#define KOUT_READ_LEN 64
#define KOUT_SECTOR   512
#define KOUT_PCI_LEN  40     /* struct pci_dev の写し (drivers/pci.h) */
#define KOUT_VER_LEN  32

int main(int argc, char **argv, KernelAPI *api)
{
    u8 sbuf[KOUT_SECTOR];              /* アプリのスタック (非恒等写像) */
    char vbuf[KOUT_VER_LEN];
    u8 pci_buf[KOUT_PCI_LEN];
    RTC_Time_Ext rtc;
    const char *path = KOUT_PATH;
    u8 *heap;
    int fails = 0, skips = 0;
    int fd, rc, i, w, h, drv, present, absent;

    if (argc > 1) path = argv[1];

    if (api->version < 60) {
        api->kprintf(0x41, "KAPI v%d < 60\n", api->version);
        return 1;
    }

    heap = (u8 *)malloc(KOUT_SECTOR);
    if (!heap) {
        api->kprintf(0x41, "malloc failed\n");
        return 1;
    }

    /* --- 1. sys_read (A 型: 長さ引数つき) -------------------------------- */
    fd = api->sys_open(path, KAPI_O_RDONLY);
    if (fd < 0) {
        api->kprintf(0x46, "1 sys_read: SKIP (open %s = %d)\n", path, fd);
        skips++;
    } else {
        for (i = 0; i < KOUT_READ_LEN; i++) sbuf[i] = 0xAA;
        rc = api->sys_read(fd, sbuf, KOUT_READ_LEN);
        if (rc <= 0) fails++;
        api->kprintf(rc > 0 ? 0xE1 : 0x41, "1a stack buf: rc=%d\n", rc);

        for (i = 0; i < KOUT_READ_LEN; i++) heap[i] = 0xAA;
        rc = api->sys_read(fd, heap, KOUT_READ_LEN);
        if (rc < 0) fails++;            /* EOF (0) は失敗ではない */
        api->kprintf(rc >= 0 ? 0xE1 : 0x41, "1b heap buf: rc=%d\n", rc);
        api->sys_close(fd);
    }
    /* fd が不正でも出力範囲を先に検査する。有効なバッファで VFS に届ける。 */
    rc = api->sys_read(99, sbuf, KOUT_READ_LEN);
    if (rc >= 0) fails++;
    api->kprintf(rc < 0 ? 0xE1 : 0x41, "1c stack buf (bad fd): rc=%d\n", rc);

    /* --- 2. np2_get_version (A 型) --------------------------------------- */
    for (i = 0; i < KOUT_VER_LEN; i++) vbuf[i] = (char)0xAA;
    api->np2_get_version(vbuf, KOUT_VER_LEN);
    rc = 0;
    for (i = 0; i < KOUT_VER_LEN; i++) if (vbuf[i] == '\0') { rc = 1; break; }
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41,
                 "2a stack buf: NUL within %d bytes=%d\n", KOUT_VER_LEN, rc);

    for (i = 0; i < KOUT_VER_LEN; i++) heap[i] = 0xAA;
    api->np2_get_version((char *)heap, KOUT_VER_LEN);
    rc = 0;
    for (i = 0; i < KOUT_VER_LEN; i++) if (heap[i] == '\0') { rc = 1; break; }
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41, "2b heap buf: NUL found=%d\n", rc);

    /* 長さ 0 は検査も書き込みも無い。np2_recv_str も maxlen<=0 で戻る。 */
    api->np2_get_version((char *)0, 0);
    api->kprintf(0xE1, "2c len=0: survived\n");
    api->kprintf(0x46, "2d NULL: N/A (NULL + 非零長は wrap で kill、ホスト試験で確認)\n");

    /* --- 3. rtc_read (B 型: 固定長 7 バイト) ----------------------------- */
    rtc.month = 0; rtc.day = 0;
    api->rtc_read(&rtc);
    rc = (rtc.month >= 1 && rtc.month <= 12 && rtc.day >= 1 && rtc.day <= 31);
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41, "3a stack: %02d/%02d %02d:%02d:%02d\n",
                 rtc.month, rtc.day, rtc.hour, rtc.min, rtc.sec);

    api->rtc_read(heap);            /* ヒープ (非恒等写像) */
    rc = (heap[1] >= 1 && heap[1] <= 12);
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41, "3b heap: month=%d\n", heap[1]);
    api->kprintf(0x46, "3c NULL: N/A (NULL + 非零長は wrap で kill、ホスト試験で確認)\n");

    /* --- 4. console_get_size (B 型: 出力 2 本) --------------------------- */
    w = 0; h = 0;
    api->console_get_size(&w, &h);
    rc = (w > 0 && h > 0);
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41, "4a both: %dx%d\n", w, h);

    /* 2 本とも必須。stack/heap の混在と heap 2 本を確認する。 */
    w = 0; ((int *)heap)[0] = 0;
    api->console_get_size(&w, (int *)heap);
    rc = (w > 0 && ((int *)heap)[0] == h);
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41, "4b stack/heap: %dx%d\n", w, ((int *)heap)[0]);
    ((int *)heap)[0] = 0; ((int *)heap)[1] = 0;
    api->console_get_size((int *)heap, (int *)heap + 1);
    rc = (((int *)heap)[0] == w && ((int *)heap)[1] == h);
    if (!rc) fails++;
    api->kprintf(rc ? 0xE1 : 0x41, "4c heap: %dx%d\n", ((int *)heap)[0], ((int *)heap)[1]);

    /* --- 5. pci_get (B 型: 40 バイト) ------------------------------------ */
    for (i = 0; i < KOUT_PCI_LEN; i++) pci_buf[i] = 0xAA;
    rc = api->pci_get(0, pci_buf);
    /* NP21/W は PCI を実装していないので count=0 → -1 が正しい姿 */
    api->kprintf(0xE1, "5a stack (count=%d): rc=%d\n", api->pci_count(), rc);
    if (api->pci_count() > 0 && rc != 0) fails++;

    rc = api->pci_get(api->pci_count(), pci_buf);
    if (rc >= 0) fails++;
    api->kprintf(rc < 0 ? 0xE1 : 0x41, "5b invalid index: rc=%d\n", rc);

    /* --- 6. ide_read_sector (B 型: 512 バイト) --------------------------- */
    present = -1; absent = -1;
    for (drv = 0; drv < 4; drv++) {
        if (api->ide_drive_present(drv)) { if (present < 0) present = drv; }
        else if (absent < 0) absent = drv;
    }
    if (present < 0) {
        api->kprintf(0x46, "6a ide_read_sector: SKIP (ドライブが無い)\n");
        skips++;
    } else {
        for (i = 0; i < KOUT_SECTOR; i++) heap[i] = 0xAA;
        rc = api->ide_read_sector(present, 0, heap);
        if (rc != 0) fails++;
        api->kprintf(rc == 0 ? 0xE1 : 0x41,
                     "6a heap 512B (drv %d): rc=%d\n", present, rc);

        for (i = 0; i < KOUT_SECTOR; i++) sbuf[i] = 0xAA;
        rc = api->ide_read_sector(present, 0, sbuf);
        if (rc != 0) fails++;
        api->kprintf(rc == 0 ? 0xE1 : 0x41, "6b stack 512B: rc=%d\n", rc);
    }
    if (absent < 0) {
        api->kprintf(0x46, "6c stack buf: SKIP (不在ドライブが無い)\n");
        skips++;
    } else {
        /* 不在ドライブのエラーも、有効な出力範囲を渡して確認する。 */
        rc = api->ide_read_sector(absent, 0, sbuf);
        if (rc >= 0) fails++;
        api->kprintf(rc < 0 ? 0xE1 : 0x41,
                     "6c stack buf (drv %d absent): rc=%d\n", absent, rc);
    }

    free(heap);
    api->kprintf(fails ? 0x41 : 0xC1, "KOUT %s (%d failure(s), %d skip(s))\n",
                 fails ? "FAIL" : "PASS", fails, skips);
    return fails ? 1 : 0;
}
