/* KAPI データ欄の固定配置と予約スロット (票 TASK_KAPI_DATA_FIELDS、v63)。
 * 配置が動くと旧バイナリの malloc が黙って全部 ENOMEM になる。予約スロットが
 * NULL だと旧 SDK の呼び出しが 0 番地へ飛ぶ。 */
static void test_kapi_layout(void)
{
    u32 bad = kst_exec_kapi_layout();
    check((bad & (1u << 0)) == 0, "kapi:data@fixed");
    check((bad & (1u << 1)) == 0, "kapi:rsv nosys");
    check((bad & (1u << 2)) == 0, "kapi:rsv stubs");
    check((bad & (1u << 3)) == 0, "kapi:hdr v3");
}
