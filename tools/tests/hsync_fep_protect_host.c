/* DICT_META F/O3: product lexical rules + unmodified hsync entity scan.
 * Freestanding ILP32. The filesystem fake starts empty, uses the product's
 * actual 24-slot arrays, and supplies only ls/stat responses (no copy of scan).
 */
#include "os32/os32_kapi_shared.h"
#include "userland/system/hsync_protect.inc"
#define CHECK(x) do { if (!(x)) return __LINE__; } while (0)
static int str_ncpy(char *dst, const char *src, int cap)
{
    int i = 0;
    while (src[i]) { if (i + 1 >= cap) return 0; dst[i] = src[i]; i++; }
    dst[i] = 0; return 1;
}
static int str_ncat(char *dst, const char *src, int cap)
{
    int i = 0;
    while (dst[i]) i++;
    return str_ncpy(dst + i, src, cap - i);
}
static int eq(const char *a, const char *b)
{ while (*a && *a == *b) { a++; b++; } return *a == *b; }
static char names[25][OS32_MAX_PATH];
static int entries, ls_rc, stat_rc, stat_calls, wrong_root;
static u32 alias_dev, alias_ino;
static const char *g_root = "";
static void quiet(int color, const char *fmt, ...) { (void)color; (void)fmt; }
static int fake_stat(const char *path, OS32_Stat *st)
{
    int i;
    stat_calls++;
    if (stat_rc) return stat_rc;
    if (eq(path, "/bin/alias")) {
        st->st_dev = alias_dev; st->st_ino = alias_ino; return 0;
    }
    for (i = 0; i < entries; i++) {
        char full[OS32_MAX_PATH];
        str_ncpy(full, g_root, sizeof(full)); str_ncat(full, "/etc/", sizeof(full));
        str_ncat(full, names[i], sizeof(full));
        if (eq(full, path)) { st->st_dev = 7; st->st_ino = i + 100; return 0; }
    }
    return OS32_ERR_NOTFOUND;
}
static struct {
    void (*kprintf)(int, const char *, ...);
    int (*sys_stat)(const char *, OS32_Stat *);
} fake_api = {quiet, fake_stat}, *api = &fake_api;
static int os32_ls(const char *dir, DirCallback cb, void *ctx)
{
    int i;
    char expected[OS32_MAX_PATH];
    str_ncpy(expected, g_root, sizeof(expected));
    str_ncat(expected, "/etc", sizeof(expected));
    if (!eq(dir, expected)) wrong_root = 1;
    if (ls_rc) return ls_rc;
    for (i = 0; i < entries; i++) {
        DirEntry_Ext entry = {0};
        str_ncpy(entry.name, names[i], sizeof(entry.name)); cb(&entry, ctx);
    }
    return 0;
}
/* HSP_ENTITY_SCAN */
static int test(void)
{
    hsp_fep_all_db = 1;
    CHECK(hsp_path_classify("/ETC/Other.dB") == 1);
    CHECK(hsp_path_classify("/etc/a.DB-journal") == 1);
    CHECK(hsp_path_classify("/etc/a.db.new/x") == 1);
    CHECK(!hsp_path_classify("/etc/a.d"));
    CHECK(!hsp_path_classify("/etc/sub/a.db"));
    CHECK(!hsp_path_classify("/else/a.db"));
    entries = 1;
    str_ncpy(names[0], "Other.DB", sizeof(names[0]));
    CHECK(scan_protected_entities() == 0 && g_prot_count == 1);
    alias_dev = 7; alias_ino = 100;
    CHECK(is_same_as_protected("/bin/alias") == 1);
    hsp_fep_all_db = 0; entries = 0;
    CHECK(!hsp_path_classify("/etc/Other.DB"));
    char path[OS32_MAX_PATH + 1];
    int i, j;
    const char *suffix[] = {"", "-journal", ".new", ".whatever", "2", 0};
    CHECK(HSP_FEP_PATH_CAP == OS32_MAX_PATH);
    CHECK(hsp_path_classify("/ETC/FEP_USER.DB/x") == 1); /* BSS, no setter */
    CHECK(scan_protected_entities() == 0 && g_prot_count == 0);
    CHECK(hsp_set_fep_user_path("/etc/./sub/../Learn.DB") == 0);
    CHECK(eq(hsp_fep_user_path, "/etc/Learn.DB"));
    str_ncpy(path, "/etc/owned.db", sizeof(path));
    CHECK(hsp_set_fep_user_path(path) == 0);
    path[5] = 'X';
    CHECK(hsp_path_classify("/etc/owned.db-journal") == 1);
    CHECK(hsp_set_fep_user_path("/etc/Learn.DB") == 0);
    for (i = 0; suffix[i]; i++) {
        str_ncpy(path, "/etc/FEP_USER.DB", sizeof(path));
        str_ncat(path, suffix[i], sizeof(path));
        CHECK(hsp_path_classify(path) == 1);
        str_ncpy(path, "/ETC/learn.db", sizeof(path));
        str_ncat(path, suffix[i], sizeof(path));
        CHECK(hsp_path_classify(path) == 1);
        str_ncat(path, "/inner/file", sizeof(path));
        CHECK(hsp_path_classify(path) == 1);
    }
    CHECK(hsp_path_classify("//etc/./x/../LEARN.db.new/inner") == 1);
    CHECK(!hsp_path_classify("/etc/sub/learn.db"));
    CHECK(!hsp_path_classify("/other/fep_user.db"));
    CHECK(!hsp_path_classify("/etc/settings.db.new2"));
    CHECK(!hsp_path_classify("/etc/learn.d"));
    CHECK(!hsp_path_classify("/etc"));
    {
        const char *bad[] = {0, "", "etc/a", "/etc", "/etc/", "/etc/a/b",
            "/else/a", "/../etc/a", "/etc/a\\b"};
        for (i = 0; i < (int)(sizeof(bad) / sizeof(bad[0])); i++) {
            CHECK(hsp_set_fep_user_path(bad[i]) == OS32_ERR_INVAL);
            CHECK(hsp_path_classify("/etc/learn.db") == 1);
        }
    }
    /* 247-byte path + 8-byte journal + NUL fits exactly; one more fails. */
    str_ncpy(path, "/etc/", sizeof(path));
    for (i = 5; i < 247; i++) path[i] = 'a';
    path[247] = 0;
    CHECK(hsp_set_fep_user_path(path) == 0);
    CHECK(hsp_path_classify(path) == 1);
    str_ncat(path, "-journal", sizeof(path));
    CHECK(hsp_path_classify(path) == 1);
    path[247] = 'a'; path[248] = 0;
    CHECK(hsp_set_fep_user_path(path) == OS32_ERR_INVAL);
    path[256] = 0;
    for (i = 248; i < 256; i++) path[i] = 'a';
    CHECK(hsp_set_fep_user_path(path) == OS32_ERR_INVAL);
    CHECK(hsp_set_fep_user_path("/etc/learn.db") == 0);
    /* A protected name beyond the old 63-byte cap still guards its inode. */
    entries = 1;
    str_ncpy(names[0], "fep_user.db", sizeof(names[0]));
    for (i = 11; i < 110; i++) names[0][i] = 'a';
    names[0][110] = 0;
    CHECK(scan_protected_entities() == 0 && g_prot_count == 1);
    CHECK(eq(g_prot_name[0], names[0]));
    alias_dev = 7; alias_ino = 100;
    CHECK(is_same_as_protected("/bin/alias") == 1);
    /* Existing 11 + default 3 + custom 3 = 17; then mixed-case/suffix names. */
    for (i = 0; hsp_protected_names[i]; i++)
        str_ncpy(names[i], hsp_protected_names[i], sizeof(names[i]));
    CHECK(i == 11);
    for (j = 0; j < 3; j++, i++) {
        str_ncpy(names[i], "fep_user.db", sizeof(names[i]));
        str_ncat(names[i], suffix[j], sizeof(names[i]));
    }
    for (j = 0; j < 3; j++, i++) {
        str_ncpy(names[i], "LEARN.DB", sizeof(names[i]));
        str_ncat(names[i], suffix[j], sizeof(names[i]));
    }
    entries = i;
    CHECK(scan_protected_entities() == 0 && g_prot_count == 17);
    for (; i < 25; i++) {
        str_ncpy(names[i], "FeP_User.Db.extra", sizeof(names[i]));
        names[i][16] = (char)('A' + i); names[i][17] = 0;
    }
    g_root = "/hd0"; entries = 24;
    CHECK(scan_protected_entities() == 0 && g_prot_count == 24 && !wrong_root);
    for (i = 0; i < 24; i++) {
        alias_dev = 7; alias_ino = i + 100;
        CHECK(is_same_as_protected("/bin/alias") == 1);
    }
    alias_dev = 8; CHECK(!is_same_as_protected("/bin/alias"));
    alias_dev = 7; alias_ino = 999; CHECK(!is_same_as_protected("/bin/alias"));
    entries = 25; stat_calls = 0;
    CHECK(scan_protected_entities() == -1 && g_prot_overflow == 1);
    CHECK(g_prot_name_count == 24 && stat_calls == 0);
    entries = 1; ls_rc = OS32_ERR_IO;
    CHECK(scan_protected_entities() == -1);
    ls_rc = OS32_ERR_NOTFOUND;
    CHECK(scan_protected_entities() == 0 && g_prot_count == 0);
    ls_rc = 0; stat_rc = OS32_ERR_IO;
    CHECK(scan_protected_entities() == -1);
    stat_rc = 0; CHECK(scan_protected_entities() == 0);
    stat_rc = OS32_ERR_IO; g_abort = 0;
    CHECK(!is_same_as_protected("/bin/alias") && g_abort);
    CHECK(hsp_depth("/etc/a") == 2 && hsp_name_fits("a", 2));
    CHECK(eq(hsp_final_label(1), "FAILED:"));
    return 0;
}
void _start(void)
{
    int rc = test();
    __asm__ volatile ("int $0x80" : : "a"(1), "b"(rc ? (rc % 254 + 1) : 0) : "memory");
    __builtin_unreachable();
}
