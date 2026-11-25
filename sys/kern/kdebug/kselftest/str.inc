static void test_str(void)
{
    static char dst[16];
    int ok;

    check(kstrlen("") == 0, "kstrlen empty");
    check(kstrlen("abcd") == 4, "kstrlen 4");

    check(kstrcmp("", "") == 0, "kstrcmp empty");
    check(kstrcmp("abc", "abd") < 0, "kstrcmp lt");
    check(kstrcmp("abd", "abc") > 0, "kstrcmp gt");
    /* 0x80 以上のバイトを含む比較。ASM 側が符号付きで比べていると
     * 大小が逆転する (CP932 の 2 バイト目が該当する) */
    check(kstrcmp("\x80", "\x01") > 0, "kstrcmp high byte unsigned");

    check(kstrncmp("abc", "abd", 0) == 0, "kstrncmp n=0");
    check(kstrncmp("abc", "abd", 2) == 0, "kstrncmp n=2");
    check(kstrncmp("abc", "abd", 3) != 0, "kstrncmp n=3");

    /* kstrncpy は strlcpy セマンティクス: n はバッファ全体サイズ、
     * 必ず NUL 終端する */
    kmemset(dst, 0x7F, sizeof(dst));
    kstrncpy(dst, "abcdefgh", 4);
    ok = (dst[0] == 'a' && dst[2] == 'c' && dst[3] == '\0');
    check(ok, "kstrncpy truncates + NUL");

    /* kstrncat: n=0 は 1 バイトも触らない (n-1 のラップで暴走した形) */
    dst[0] = 'X'; dst[1] = '\0';
    kstrncat(dst, "yyyy", 0);
    check(dst[0] == 'X' && dst[1] == '\0', "kstrncat n=0");

    /* kstrncat: バッファぴったりで切り詰め + NUL */
    kstrcpy(dst, "abc");
    kstrncat(dst, "defgh", 6);
    ok = (kstrcmp(dst, "abcde") == 0);
    check(ok, "kstrncat exact fit");

    /* kstrncat: 既に満杯なら何もしない */
    kstrcpy(dst, "abcde");
    kstrncat(dst, "zzz", 6);
    check(kstrcmp(dst, "abcde") == 0, "kstrncat already full");

    /* strchr は NUL 自身も見つける契約 */
    check(strchr("abc", '\0') != (char *)0, "strchr finds NUL");
    check(strchr("abc", 'z') == (char *)0, "strchr not found");
}
