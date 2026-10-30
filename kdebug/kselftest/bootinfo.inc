/* PIT の分周が**判定したクロックに従っている**こと (票 TASK_HAL_WIRING §1-0)。
 *
 * 直す前の `pit_init()` は 1.9968MHz 決め打ちで割っていた。2.4576MHz 系
 * (実機 PC-9821Ra266、0000:0501h bit7 = 0) では 24576 を積むべきところに
 * 19968 が入り、100Hz のつもりの tick が **123Hz = 8.125ms** になる。
 * tick を数える待ち・番犬・CPU 校正がまとめて 23% 速くなるが、
 * **NP21/W は 1.9968MHz 設定なので一度も踏めない**。だからここで起動時に
 * 実機の値そのものを見る (ホスト試験 tools/tests/test_pit_clock.py は
 * 同じ算数を両クロックで見る)。 */
/* ------------------------------------------------------------------------ */
/*  ブート情報域 (票 TASK_HDD_INSTALL 段 0)                                  */
/*                                                                          */
/*  起動時の情報域そのものは FD 起動 / HDD 起動 / HDD 無しで答えが違うので    */
/*  「写したこと」と、検証関数が良い域を通し壊れた域を断ることだけを見る。   */
/*  規則の網羅はホスト試験 (tools/tests/test_bootinfo.py)。                 */
/* ------------------------------------------------------------------------ */
static void test_bootinfo(void)
{
    u8 raw[BOOTINFO_WIRE_SIZE];
    struct bootinfo bi;
    u8 *d0 = raw + BI_OFF_DRIVE0;
    u16 c = 0, sec = 0;
    u8 h = 0, s = 0;
    int i;

    /* kernel_main の最初で写している (NONE のままなら呼び忘れ)。 */
    check(bootinfo_get()->status != BOOTINFO_ERR_NONE, "bootinfo:captured");
    /* 写した後は低位の magic を消している (次の起動で残りを読まない)。 */
    check(*(volatile u32 *)P2V_IO(MEM_BOOTINFO_BASE) != BOOTINFO_MAGIC,
          "bootinfo:low magic cleared");
    /* 有効な域なら、使えると言ったドライブの幾何が取れる。 */
    if (bootinfo_get()->status == BOOTINFO_OK &&
        bootinfo_get()->drive[0].valid) {
        check(bootinfo_hdd_geom(bootinfo_get()->drive[0].da, &c, &h, &s, &sec) == 0
              && sec == 512 && h != 0 && s != 0, "bootinfo:geom");
    }
    /* v2 のローダ (FD / HDD) は vmkernel.lz4 を検査し終えてから、起動した
     * イメージの CRC をイメージ欄に残す (票 TASK_SERIAL_HOSTFS A-4)。
     * 主部が有効なのに記録が無ければ、ローダが古いか書き忘れ。 */
    if (bootinfo_get()->status == BOOTINFO_OK) {
        check(bootinfo_get()->img_valid == 1 && bootinfo_get()->img_size != 0,
              "bootinfo:image crc recorded");
    }

    for (i = 0; i < (int)sizeof(raw); i++) raw[i] = 0;
    raw[BI_OFF_VERSION] = (u8)BOOTINFO_VERSION;
    raw[BI_OFF_SOURCE] = (u8)BOOTINFO_SRC_FD;
    raw[BI_OFF_NDRIVES] = (u8)BOOTINFO_NDRIVES;
    d0[BI_DRV_DA] = 0x80; d0[BI_DRV_VALID] = 1; d0[BI_DRV_QUERIED] = 1;
    d0[BI_DRV_BX] = 0x00; d0[BI_DRV_BX + 1] = 0x02;       /* 512 */
    d0[BI_DRV_CX] = 0x10; d0[BI_DRV_CX + 1] = 0x01;       /* 272 */
    d0[BI_DRV_DH] = 8; d0[BI_DRV_DL] = 17;
    bootinfo_seal(raw);
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK &&
          bi.drive[0].valid == 1 && bi.drive[0].cyl == 272, "bootinfo:good");
    bootinfo_seal_image(raw, 0x12345678UL, 447337UL);
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK &&
          bi.img_valid == 1 && bi.img_crc == 0x12345678UL && bi.img_size == 447337UL,
          "bootinfo:image good");
    raw[BI_OFF_IMG_CRC] ^= 1;
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_OK && bi.img_valid == 0,
          "bootinfo:image bad check");
    raw[BI_OFF_CHECK] ^= 1;
    check(bootinfo_parse(raw, sizeof(raw), &bi) == BOOTINFO_ERR_CHECK,
          "bootinfo:bad check");
}
