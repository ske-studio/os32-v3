/* ------------------------------------------------------------------------ */
/*  kprintf                                                                 */
/*                                                                          */
/*  出力内容は取れないので「戻ってくること」を確かめる。書式末尾が '%' の    */
/*  ときに無限ループしていた (KAPI 公開関数なので、ユーザプログラムの        */
/*  1 行でカーネルが固まった)。ここで固まればブートが止まるので分かる。      */
/* ------------------------------------------------------------------------ */
static void test_kprintf(void)
{
    kprintf(KLOG_ATTR, "%0");
    kprintf(KLOG_ATTR, "%-04l");
    check(1, "kprintf incomplete modifiers return at NUL");
    kprintf(KLOG_ATTR, "");
    kprintf(KLOG_ATTR, "%");             /* 末尾 % — 旧実装はここで無限ループ */
    kprintf(KLOG_ATTR, "%%");
    kprintf(KLOG_ATTR, "%z", 1);         /* 未知指定子 (vararg を消費する) */
    kprintf(KLOG_ATTR, "\n");
    check(1, "kprintf returns (no hang)");
}
