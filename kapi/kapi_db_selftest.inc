/* DB slot layout and v50 contract self-tests. */
/* ======================================================================== */
/*  db_slot_layout_ok — KAPI 表の slot 配置が v50 / v51 の形を保っているか    */
/*                                                                          */
/*  見るのは **既存スロットの位置**であって、表の長さではない。              */
/*                                                                          */
/*  ここは以前 `KAPI_SLOT_COUNT != KAPI_SLOT_HOST_CLOSE + 1` と書いていた。   */
/*  それは「host_close の後ろに 1 本も足されていないこと」を要求するので、    */
/*  **[ABI] が正当と定めた末尾追記のたびに必ず落ちる**。実際 2026-09-15 に   */
/*  票 H3 の `sys_set_mtime` (slot 213) で `make check` が落ちた。           */
/*  この項の意図は「追記した 7 本が 201..207 に居て、既存の `db_*` が        */
/*  動いていない」であって、表がそこで終わっていることではない。             */
/*                                                                          */
/*  だから長さは **下限だけ** 見る。slot_count を引数で受けるのは、          */
/*  「KAPI をもう 1 本足したら」をホスト試験から直に試せるようにするため      */
/*  (tools/tests/kapi_db_v50_host.c の slot_layout_append)。                 */
/*                                                                          */
/*  数値の直書きはしない ([C4]) — 位置は全部 os32_kapi_slots.h の定数から     */
/*  導く。合っていなければ外部プログラムは「別の関数を呼ぶ」という最も静かな  */
/*  壊れ方をする。                                                          */
/* ======================================================================== */
int db_slot_layout_ok(int slot_count)
{
    /* 表は少なくとも既知の末尾 slot を含む長さがある (**上限は見ない**) */
    if (slot_count <= KAPI_SLOT_HOST_CLOSE) return 0;

    /* v50 で追記した 7 本が 201..207 に居る */
    if (KAPI_SLOT_DB_OPEN_EXISTING != 201) return 0;
    if (KAPI_SLOT_DB_ERROR_CODE != 207) return 0;          /* db 帯の末尾 */

    /* 既存 10 本は動いていない */
    if (KAPI_SLOT_DB_OPEN != 140) return 0;

    /* v51 の host 帯 5 本が 208..212 に居る。以前は「件数 == host_close + 1」
     * が間接的に押さえていただけだったので、**位置を明示して**見る。 */
    if (KAPI_SLOT_HOST_OPEN != 208) return 0;
    if (KAPI_SLOT_HOST_CLOSE != 212) return 0;

    return 1;
}

/* ======================================================================== */
/*  db_v50_selftest — ホスト試験が使う v50 の骨 (私有 import は保持)      */
/* ======================================================================== */
u32 db_v50_selftest(void)
{
    u32 bad = 0;
    u32 hdr = (u32)sizeof(DB_ResultHeader);
    u32 desc = (u32)sizeof(DB_ColumnInfo);

    /* (0) slot 配置。判定は db_slot_layout_ok() (下) が持つ。 */
    if (!db_slot_layout_ok(KAPI_SLOT_COUNT)) bad |= 1u << 0;

    /* (1) SHM の境界: ちょうど収まる / 1 バイト超過 / descriptor だけで溢れる。
     * 上限は**結果側の上限** (診断領域を除いた分)。 */
    if (!shm_row_fits_n(1, (u32)DB_SHM_RESULT_LIMIT - hdr - desc)) bad |= 1u << 1;
    if (shm_row_fits_n(1, (u32)DB_SHM_RESULT_LIMIT - hdr - desc + 1u)) bad |= 1u << 1;
    if (shm_row_fits_n((int)(((u32)DB_SHM_RESULT_LIMIT - hdr) / desc) + 1, 0))
        bad |= 1u << 1;
    if (shm_row_fits_n(-1, 0)) bad |= 1u << 1;

    /* (2) ポインタ検証の NULL / overflow (帯と PTE は CPL=3 側でしか踏めない) */
    if (db_copy_input((const void *)0, path_copy_buf, 1u, 0)) bad |= 1u << 2;
    if (db_copy_input((const void *)0xFFFFFF00UL, sql_copy_buf, 0x200u, 0)) bad |= 1u << 2;

    /* (3) journal 名の容量規則 (票 §1a / Codex 往復 2 の B4)。
     * 末尾判定は SQLite 自身にさせるので接続が要る = ブート時には踏めない。
     * ここで踏むのは接続の要らない「下位層の path 容量」の方。
     * `<絶対名>-journal` が NUL 込み VFS_MAX_PATH に収まる長さが境界。 */
    {
        static char probe[VFS_MAX_PATH + 8];
        u32 fit = (u32)VFS_MAX_PATH - 1u - ((u32)sizeof(DB_JOURNAL_SUFFIX) - 1u);
        u32 k;
        for (k = 0; k < (u32)sizeof(probe); k++) probe[k] = 'a';
        probe[0] = '/';
        probe[fit] = '\0';
        if (!db_journal_name(probe)) bad |= 1u << 3;
        probe[fit] = 'a';
        probe[fit + 1u] = '\0';
        if (db_journal_name(probe)) bad |= 1u << 3;
    }

    /* (4) owner 別の欄が ID の池 (1 = シェル帯 .. 5) を覆っている */
    if (DB_OWNER_SLOTS < 6) bad |= 1u << 4;

    /* (5) 診断領域と結果領域がブロック 0 を過不足なく分け合っている
     * (票 TASK_DB_ERRSTR §4)。ここがずれると、結果データが
     * db_last_error() の返り先を踏み潰す。 */
    /* 結果の上限がそのまま診断領域の入口 = 隙間も重なりも無い */
    if ((u32)DB_SHM_DIAG_OFFSET != (u32)DB_SHM_RESULT_LIMIT) bad |= 1u << 5;
    /* 診断文のバッファと「常に NUL の 1 バイト」が重ならない */
    if ((u32)DB_SHM_DIAG_OFFSET + (u32)DB_SHM_ERRSTR_MAX
        > (u32)DB_SHM_EMPTY_OFFSET) bad |= 1u << 5;
    /* その 1 バイトは診断領域の中 (= ブロック 0 の中) にある */
    if ((u32)DB_SHM_EMPTY_OFFSET - (u32)DB_SHM_DIAG_OFFSET
        >= (u32)DB_SHM_DIAG_SIZE) bad |= 1u << 5;
    /* 空文字列は必ず NUL で読める (返す側が毎回置く) */
    if (db_shm_empty()[0] != '\0') bad |= 1u << 5;
    /* 上限を超える診断文は切られ、NUL で終わり、診断領域を出ない */
    {
        static char probe[DB_SHM_ERRSTR_MAX + 16];
        const char *cut;
        u32 k;
        for (k = 0; k < (u32)sizeof(probe) - 1u; k++) probe[k] = 'x';
        probe[sizeof(probe) - 1] = '\0';
        cut = db_shm_diag(probe);
        if (cut != (const char *)(DB_SHM_PTR + DB_SHM_DIAG_OFFSET))
            bad |= 1u << 5;
        if (kstrlen(cut) != (u32)DB_SHM_ERRSTR_MAX - 1u) bad |= 1u << 5;
    }

    return bad;
}

