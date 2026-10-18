/* FDRECOV-1: 実物 hsync を通す受入。H4 の贋 FS・カウンタを共用する。 */
static void fdrecov_setup(int header_case, int other)
{
    OS32Header h = fixture_shell_header();
    u32 len = sizeof(h);
    int n;
    setup_tree();
    fixture_shell_enabled = 0;
    fk_other_mount = other;
    g_inject_force_kapi = 0;
    g_fake.version = KAPI_VERSION;
    n = fs_add_dir("/");
    fs_nodes[n].ino = HS_EXT2_ROOT_INO;
    fs_add_dir("/host/boot");
    fs_add_file("/host/boot/vmkernel.lz4", (const u8 *)"new kernel", 10);
    fs_add_file("/host/boot/kdebug.bin", (const u8 *)"paired debug", 12);
    fs_add_file("/host/boot/sqlite.mod", (const u8 *)"paired sqlite", 13);
    fs_add_file("/host/boot/fep.mod", (const u8 *)"paired fep", 10);
    fs_add_dir("/host/sys");
    fs_add_file("/host/sys/unicode.bin", (const u8 *)"unicode table", 13);
    switch (header_case) {
    case 1: h.memory_layout_generation++; break;
    case 2: h.kapi_abi_generation++; break;
    case 3: h.shlib_protocol = OS32_SHLIB_PROTOCOL + 1; break;
    case 4: h.kapi_data_off += 4; break;
    case 5: h.magic = 0; break;
    case 6: h.header_size = OS32X_HDR_V3_SIZE; break;
    case 7: h.version++; break;
    case 8: h.flags = 0x80000000U; break;
    case 9: len--; break;
    case 11: h.shlib_protocol = OS32_SHLIB_PROTOCOL; break;
    case 12: h.min_api_ver = KAPI_VERSION + 1; break;
    default: break;
    }
    if (header_case != 10)
        fs_add_file("/host/sys/shell.bin", (const u8 *)&h, len);
    if (other) {
        n = fs_add_dir("/hd0");
        fs_nodes[n].ino = HS_EXT2_ROOT_INO;
        fs_nodes[n].dev = other == 1 ? 2 : 1;
    }
    put_good_manifest("fdrecov");
}

static void fdrecov_manifest_version(u32 off, u32 ver)
{
    char text[1024];
    snprintf(text, sizeof(text),
             "format=2\nbuild=fdrecov\ngenerated=test\nkapi=%u\nkapi_version=%u\n"
             "count=2\n---\nbin/a.bin %u %08x 111\nbin/b.bin %u %08x 222\n",
             (unsigned)off, (unsigned)ver,
             (unsigned)g_a_size, (unsigned)g_a_crc,
             (unsigned)g_b_size, (unsigned)g_b_crc);
    fs_set_data(fs_find(MAN_PATH), (const u8 *)text, (u32)strlen(text));
}

static void case_fdrecov(void)
{
    static const char *const targets[] = {"boot", "sys", 0};
    static const char *const flags[] = {"-n", "-f", "--force-kapi", 0};
    int h, t, f, rc;
    u32 saved_ver = g_fake.version;
    printf("== FDRECOV-1: 世代の門と別根全体復旧 ==\n");
    for (h = 1; h <= 10; h++) {
        for (t = 0; t < 3; t++) {
            for (f = 0; f < 4; f++) {
                char *av[5] = {"hsync", 0, 0, 0, 0};
                int argc = 1;
                char msg[100];
                fdrecov_setup(h, 0);
                if (flags[f]) av[argc++] = (char *)flags[f];
                if (targets[t]) av[argc++] = (char *)targets[t];
                rc = run_hsync(argc, av);
                snprintf(msg, sizeof(msg), "header=%d target=%d flag=%d: 非0・全書込み API 0", h, t, f);
                check(rc != 0 && wrote_nothing(), msg);
                check(log_has("0 files written") && log_has("--root /hd0 --verify --no-backup"),
                      "FD 全体復旧の手順を出す");
                if (h == 1)
                    check(log_has("reason=memory_layout_mismatch") && log_has("host=") && log_has("running="),
                          "配置世代の両値を表示する");
                if (h == 3)
                    check(log_has("reason=shlib_protocol_mismatch"),
                          "現行と異なる shlib protocol は拒否する");
            }
        }
    }
    fdrecov_setup(1, 0);
    check(run3("--root", "/", "boot") != 0 && wrote_nothing(), "--root / も稼働ルートの門");
    fdrecov_setup(1, 2);
    check(run2("--root", "/hd0") != 0 && wrote_nothing(), "同一デバイス別名を全体復旧と扱わない");
    fdrecov_setup(0, 2);
    check(run2("--root", "/hd0") == 0 && fs_find("/hd0/sys/shell.bin") < 0,
          "同一デバイス別名の全体も通常の sys 除外");

    /* min_api_ver のみ上昇 / protocol=0・現行値は boot 先行が通る。 */
    fdrecov_setup(12, 0);
    fdrecov_manifest_version(KAPI_DATA_FIELDS_OFF, KAPI_VERSION + 1);
    check(run1("boot") == 0 && fs_find("/boot/vmkernel.lz4") >= 0 &&
          fs_find("/boot/kdebug.bin") >= 0 && fs_find("/boot/sqlite.mod") >= 0 &&
          fs_find("/boot/fep.mod") >= 0,
          "同世代で KAPI 版のみ上昇した boot はカーネルと対部品を運ぶ");
    fdrecov_setup(0, 0);
    check(run1("boot") == 0, "shlib protocol=0 は依存なし");
    check(fs_find("/boot/fep.mod") >= 0 &&
          fs_nodes[fs_find("/boot/fep.mod")].size == 10 &&
          memcmp(fs_nodes[fs_find("/boot/fep.mod")].data, "paired fep", 10) == 0,
          "同世代 boot は FEP のバイト列を保存する");
    fdrecov_setup(11, 0);
    check(run1("boot") == 0, "現行 shlib protocol は受理する");

    for (h = 0; h <= 12; h++) {
        if (h == 10) continue;  /* 欠落は後で拒否を確認 */
        fdrecov_setup(h, 1);
        fdrecov_manifest_version(KAPI_DATA_FIELDS_OFF + 4, KAPI_VERSION + 1);
        check(run3("--root", "//hd0/./", "--verify") == 0 &&
              fs_find("/hd0/sys/shell.bin") >= 0 &&
              fs_find("/hd0/boot/kdebug.bin") >= 0 &&
              fs_find("/hd0/boot/sqlite.mod") >= 0 &&
              fs_find("/hd0/boot/fep.mod") >= 0 &&
              fs_find("/hd0/sys/unicode.bin") >= 0 && fk_header_reads == 0,
              "別デバイス全体は異世代・不正ヘッダも解釈せず sys までコピー");
        check(log_has("manifest_extra=6"), "余剰 boot/sys は集計だけで拒否しない");
        check(fs_find("/hd0/boot/fep.mod") >= 0 &&
              fs_nodes[fs_find("/hd0/boot/fep.mod")].size == 10 &&
              memcmp(fs_nodes[fs_find("/hd0/boot/fep.mod")].data, "paired fep", 10) == 0,
              "別 root 全体復旧は FEP のバイト列を保存する");
    }
    fdrecov_setup(9, 1);
    check(run3("--root", "/hd0", "-n") == 0 && wrote_nothing() && fk_header_reads == 0,
          "復旧 dry-run もヘッダを読まない");
    fdrecov_setup(1, 1);
    check(run3("--root", "/hd0", "boot") == 0 && fk_header_reads == 0,
          "別根部分同期は shell の世代を検査しない");
    fdrecov_setup(0, 1);
    fdrecov_manifest_version(KAPI_DATA_FIELDS_OFF + 4, KAPI_VERSION);
    check(run3("--root", "/hd0", "boot") != 0 && wrote_nothing(),
          "別根部分同期は既存の名札 KAPI の門を保つ");

    for (h = 0; h < 2; h++) {
        fdrecov_setup(0, 1);
        fs_drop(fs_find(h ? "/host/sys/shell.bin" : "/host/boot/vmkernel.lz4"));
        check(run3("--root", "/hd0", "--force-kapi") != 0 && wrote_nothing() &&
              log_has("recovery_source_missing"), "必須ファイルの欠落は全書込み前に拒否");
    }
    for (h = 0; h < 3; h++) {
        fdrecov_setup(0, 1);
        if (h == 0) fs_drop(fs_find(MAN_PATH));
        else {
            const char *bad = h == 1 ? "format=1\nbuild=old\n" : "broken";
            fs_set_data(fs_find(MAN_PATH), (const u8 *)bad, (u32)strlen(bad));
        }
        check(run3("--root", "/hd0", "--force-kapi") != 0 && wrote_nothing(),
              "全体復旧は force でも有効な format=2 が必須");
    }
    fdrecov_setup(0, 1);
    g_fake.version = HS_MIN_KAPI_H2 - 1;
    check(run3("--root", "/hd0", "--unsafe-overwrite") != 0 && wrote_nothing(),
          "旧直接上書きへ落とさず対応 FD を案内する");

    fdrecov_setup(0, 1);
    fs_nodes[fs_find("/host/bin/a.bin")].data[0] ^= 1;
    check(run2("--root", "/hd0") != 0 && g_errors == 1 &&
          log_has("manifest_crc_mismatch") && fs_find("/hd0/bin/a.bin") < 0 &&
          fs_find("/hd0/bin/b.bin") >= 0, "流し CRC 不一致はその 1 件の errors、公開しない");
    fdrecov_setup(0, 1);
    check(run2("--root", "/hd0") == 0 && g_errors == 0, "CRC 正常の対照");

    fixture_shell_enabled = 1;
    fk_other_mount = 0;
    g_fake.version = saved_ver;
    g_inject_force_kapi = 1;
}
