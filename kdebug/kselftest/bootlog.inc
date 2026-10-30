/* ------------------------------------------------------------------------ */
/*  起動ログ (init/bootlog.c)。ここは書き出しの前なので、溜まっている     */
/*  最中の本物を見る。compose は本文を動かさないので呼んでも害は無い。       */
/*  組み方と手順の網羅はホスト試験 (tools/tests/test_bootlog.py)。          */
/* ------------------------------------------------------------------------ */
static void test_bootlog(void)
{
    char hdr[BOOTLOG_HDR_MAX];
    BootlogHeaderInfo hi;
    const char *out;
    u32 hl, n = 0, i;
    int same = 1;

    check(bootlog_is_active(), "bootlog still collecting before the shell");
    check(bootlog_len() > 0, "bootlog has the boot messages");
    kmemset(&hi, 0, sizeof(hi));
    hi.build = "b";
    hi.commit = "c";
    hl = bootlog_format_header(hdr, (u32)sizeof(hdr), &hi);
    check(hl > 16 && kstrncmp(hdr, "# OS32 boot log ", 16) == 0 &&
          hdr[hl - 1] == '\n', "bootlog header line");
    out = bootlog_compose(hdr, &n);
    for (i = 0; i < hl; i++) if (out[i] != hdr[i]) same = 0;
    check(same && n > hl + bootlog_len() && out[n - 1] == '\n',
          "bootlog compose = header + text + end line");
}
