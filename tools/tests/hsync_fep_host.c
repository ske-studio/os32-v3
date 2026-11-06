/* Reuse the existing RAM FS, production hsync main and fault injection. */
#include "fep_boundary_runtime.h"
static char *strncpy(char *dst, const char *src, size_t n)
{
    size_t i;
    for (i = 0; i < n && src[i]; i++) dst[i] = src[i];
    for (; i < n; i++) dst[i] = 0;
    return dst;
}
extern int sprintf(char *dst, const char *fmt, ...);
extern char *strcat(char *dst, const char *src);
#include "hsync_h2_host.c"
static int fep_closes, fep_opens, fep_close_rc, fep_open_rc;
static int fep_closed, fep_writes_before_close, fep_abort;
static int fake_fep_suspend(int on)
{
    if (on) {
        fep_closes++;
        fep_writes_before_close = fk_write_calls;
        if (fep_close_rc) return fep_close_rc;
        fep_closed = 1;
        if (fep_abort) g_abort = 1;
    } else {
        fep_opens++;
        /* Count unexpected reopen too; cases assert the exact call count. */
        fep_closed = 0;
        if (fep_open_rc) return fep_open_rc;
    }
    return 0;
}
static void prepare(void)
{
    fixture_used = 0; /* Previous invocation is finished; free is a bump-runtime no-op. */
    setup_pair(32, 1, 200, 16, 2, 100);
    fake_api_init();
    fk_version = KAPI_VERSION;
    g_fake.ime_dict_suspend = fake_fep_suspend;
    fep_closes = fep_opens = fep_close_rc = fep_open_rc = 0;
    fep_closed = fep_writes_before_close = fep_abort = 0;
}
int main(void)
{
    int rc;
    prepare(); rc = run2("--fep-off", "bin");
    check(!rc && fep_closes == 1 && fep_opens == 1 && !fep_closed && fk_write_calls > 0 && !fep_writes_before_close, "fep success ordered");
    prepare(); fk_write_fail_at = 1; rc = run2("--fep-off", "bin");
    check(rc && fep_closes == 1 && fep_opens == 1 && !fep_closed, "fep write failure reopens");
    prepare(); fep_abort = 1; rc = run2("--fep-off", "bin");
    check(rc && fep_opens == 1 && !fep_closed, "fep abort reopens");
    prepare(); fep_close_rc = -7; rc = run2("--fep-off", "bin");
    check(rc && !fk_write_calls && !fk_sync_calls && fep_closes == 1 && !fep_opens && log_has("FEP suspend (err=-7)"), "fep close failure stops");
    prepare(); rc = run2("--fep-off", "-n");
    check(!rc && !fep_closes && !fep_opens && !fk_write_calls, "fep dry-run untouched");
    prepare(); rc = run1("bin");
    check(!rc && !fep_closes && !fep_opens, "fep default untouched");
    prepare(); fep_open_rc = -8; rc = run2("--fep-off", "bin");
    check(rc && fep_opens == 1 && g_errors == 1 && log_has("FEP reopen (err=-8)"), "fep reopen error counted");
    prepare(); fk_version = 80; rc = run2("--fep-off", "bin");
    check(rc && !fep_closes && !fk_write_calls && log_has("FEP suspend"), "fep old kernel rejected");
    prepare(); fk_version = 80; rc = run2("--fep-off", "-n");
    check(!rc && !fep_closes, "fep old kernel dry-run");
    for (int fault = 2; fault <= 7; fault++) {
        prepare(); fixture_user_path = "/etc/learn.db"; fixture_settings_fault = fault;
        rc = run2("--fep-off", "bin");
        check(rc && !fk_write_calls && !fk_sync_calls && !fep_closes &&
              log_has("fep_user_dict_path unavailable or invalid"),
              "settings failure stops before sync");
    }
    /* No DB, no table, and no row all retain the default protection. */
    for (int missing = 0; missing < 3; missing++) {
        prepare();
        fixture_settings_fault = missing == 0 ? 1 : missing == 1 ? 8 : 0;
        rc = run2("--fep-off", "bin");
        check(!rc && fk_write_calls > 0 && fep_closes == 1 && fep_opens == 1 &&
              hsp_path_classify("/etc/fep_user.db-journal") == 1 &&
              !hsp_path_classify("/etc/learn.db"), "missing settings continues sync");
    }
    prepare(); fixture_settings_fault = 1;
    int node = fs_add_file("/etc/settings.db", 0, 0);
    rc = run1("bin");
    check(rc && !fk_write_calls, "existing unreadable DB stops");
    fs_nodes[node].stat_err = OS32_ERR_IO;
    rc = run1("bin");
    check(rc && !fk_write_calls, "settings stat failure stops");
    prepare(); fixture_user_path = "/other/learn.db";
    rc = run1("bin");
    check(rc && !fk_write_calls && !fk_sync_calls, "invalid custom path stops");
    prepare(); fixture_user_path = "/etc/./learn.db";
    rc = run1("bin");
    check(!rc && hsp_path_classify("/etc/learn.db-journal") == 1 &&
          hsp_path_classify("/etc/fep_user.db.new") == 1, "custom settings registered");
    prepare(); rc = run1("bin");
    check(!rc && !hsp_path_classify("/etc/learn.db"), "setting reset between runs");
    prepare(); api = &g_fake;
    str_ncpy(g_root, "/hd0", (int)sizeof(g_root));
    fixture_user_path = "/etc/learn.db";
    check(!register_fep_user_path() &&
          !str_cmp(fixture_settings_path, "/hd0/etc/settings.db") &&
          hsp_path_classify("/etc/learn.db.new") == 1, "destination root setting");
    printf("%d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
