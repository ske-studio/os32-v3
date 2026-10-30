/* ------------------------------------------------------------------------ */
/*  設定レジストリの基盤 (票 S0-K、KAPI v50)                                 */
/*                                                                          */
/*  ここで踏むのは 2 つだけ。(a) KAPI の表が v50 の形か — 末尾追記の 7 本が   */
/*  201..207 に居て既存 db_* が動いていないこと。ずれると外部プログラムは     */
/*  「別の関数を呼ぶ」という最も静かな壊れ方をする。(b) 16KB の結果ブロックの */
/*  境界検査 — header + 全列 descriptor + payload が溢れる行で範囲外へ書かず  */
/*  部分 ROW も返さないこと (票 §1b)。                                       */
/*  ホスト試験 (tools/tests/test_kapi_db_v50.py) と同じ判定を使う。          */
/* ------------------------------------------------------------------------ */
static void test_db_v50(void)
{
    u32 bad = db_v50_selftest();
    check((bad & (1u << 0)) == 0, "KAPI v50: 7 new db slots appended at 201..207");
    check((bad & (1u << 1)) == 0, "db row: 16KB block bound counts descriptors");
    check((bad & (1u << 2)) == 0, "db ptr: NULL and length overflow refused");
    check((bad & (1u << 3)) == 0, "db path: journal name fits the VFS capacity");
    check((bad & (1u << 4)) == 0, "db diag: one open-failure slot per owner ID");
    /* 票 TASK_DB_ERRSTR: db_last_error() の返り先 (SHM 末尾の診断領域) が
     * 結果データと重なっていないこと。重なると結果が診断文を踏み潰す。 */
    check((bad & (1u << 5)) == 0, "db diag: error string area is outside the result area");
    /* owner 別の欄が ID の池を覆っているか (kapi_db.h の DB_OWNER_SLOTS)。 */
    check(DB_OWNER_SLOTS >= APP_SLOT_COUNT,
          "db diag: DB_OWNER_SLOTS covers the whole app ID pool");
}
