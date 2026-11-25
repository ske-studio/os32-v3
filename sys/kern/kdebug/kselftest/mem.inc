/* ------------------------------------------------------------------------ */
/*  kstring_asm.asm (kmemcpy/kmemset/kstrlen/kstrcmp/kstrncpy)              */
/*                                                                          */
/*  ASM 実装は 4 バイト単位の高速パスと 1 バイトずつの端数パスを持つので、  */
/*  n が 0 / 端数 / 境界ちょうど のときに崩れやすい。番兵を置いて           */
/*  「書きすぎていないこと」まで見る。                                       */
/* ------------------------------------------------------------------------ */
static void test_mem(void)
{
    static u8 buf[32];
    static u8 src[32];
    u32 i;
    int ok;

    for (i = 0; i < 32; i++) src[i] = (u8)(i + 1);

    /* kmemset: n=0 は 1 バイトも書いてはいけない */
    buf[0] = 0xAA;
    kmemset(buf, 0x55, 0);
    check(buf[0] == 0xAA, "kmemset n=0");

    /* kmemset: 端数長 (高速パスと端数パスの境目) */
    for (i = 0; i < 32; i++) buf[i] = 0xAA;
    kmemset(buf, 0x55, 5);
    ok = (buf[0] == 0x55 && buf[4] == 0x55 && buf[5] == 0xAA);
    check(ok, "kmemset n=5 no overrun");

    /* kmemcpy: n=0 */
    buf[0] = 0xAA;
    kmemcpy(buf, src, 0);
    check(buf[0] == 0xAA, "kmemcpy n=0");

    /* kmemcpy: 端数長 + 番兵 */
    for (i = 0; i < 32; i++) buf[i] = 0xAA;
    kmemcpy(buf, src, 7);
    ok = (buf[0] == 1 && buf[6] == 7 && buf[7] == 0xAA);
    check(ok, "kmemcpy n=7 no overrun");

    /* kmemcpy: 非アラインの dst/src */
    for (i = 0; i < 32; i++) buf[i] = 0xAA;
    kmemcpy(buf + 1, src + 1, 9);
    ok = (buf[0] == 0xAA && buf[1] == 2 && buf[9] == 10 && buf[10] == 0xAA);
    check(ok, "kmemcpy unaligned");

    /* memmove: 前方に重なるコピー (kmemcpy では壊れる形) */
    for (i = 0; i < 32; i++) buf[i] = (u8)(i + 1);
    memmove(buf, buf + 2, 8);
    ok = (buf[0] == 3 && buf[7] == 10);
    check(ok, "memmove overlap forward");

    /* memmove: 後方に重なるコピー */
    for (i = 0; i < 32; i++) buf[i] = (u8)(i + 1);
    memmove(buf + 2, buf, 8);
    ok = (buf[2] == 1 && buf[9] == 8);
    check(ok, "memmove overlap backward");

    /* memmove: n=0 と同一ポインタ */
    buf[0] = 0x42;
    memmove(buf, buf, 8);
    memmove(buf, buf + 1, 0);
    check(buf[0] == 0x42, "memmove n=0 / same ptr");
}
