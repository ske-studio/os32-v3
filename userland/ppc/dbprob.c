#include "os32api.h"
#include <string.h>

static KernelAPI *api;
static const char path[] = "/tmp/T.DB";

static void put(const char *s)
{
    while (*s) api->shell_putchar(*s++, 7);
}

static void number(u32 n)
{
    if (n >= 10) number(n / 10);
    api->shell_putchar('0' + n % 10, 7);
}

static int report(const char *name, int ok)
{
    put("DB-"); put(name); put(ok ? " OK\n" : " FAIL\n");
    return ok;
}

#define CHECK(name, expr) do { if (!report(name, (expr))) return 1; } while (0)

static int column(int h, int col, const char *wanted)
{
    const char *value = api->db_column_text(h, col);
    return value && !strcmp(value, wanted);
}

static void signed_number(int value)
{
    if (value < 0) put("-");
    number(value < 0 ? 0u - (u32)value : (u32)value);
}

static int conversion(int h, const char *name, const char *sql,
                      const char *expected_text, int expected_int)
{
    int rc = api->db_prepare(h, sql);
    const char *text = 0;
    int value = 0;
    if (rc == DB_STATUS_ROW) {
        if (expected_text) {
            text = api->db_column_text(h, 0);
            if (text && !strcmp(text, expected_text)) return report(name, 1);
        } else {
            value = api->db_column_int(h, 0);
            if (value == expected_int) return report(name, 1);
        }
    }

    put("DB-"); put(name); put(" FAIL prepare="); signed_number(rc);
    if (rc != DB_STATUS_ROW) {
        const char *error = api->db_last_error(h);
        put(" error="); put(error ? error : "(null)");
    } else {
        put(" value=");
        if (expected_text) put(text ? text : "(null)");
        else signed_number(value);
    }
    put("\n");
    return 0;
}

static int count_rows(int h, int n)
{
    return api->db_prepare(h, "SELECT count(*) FROM t") == DB_STATUS_ROW &&
           api->db_column_int(h, 0) == n && api->db_finalize(h) == 0;
}

static int names(int h)
{
    const char *short_name = "/tmp/12345678901234567890123";
    const char *long_name = "/tmp/123456789012345678901234";
    OS32_Stat st;
    memset(&st, 0xa5, sizeof(st));
    if (api->db_prepare(h, "PRAGMA journal_mode") != DB_STATUS_ROW ||
        !column(h, 0, "delete") || api->db_finalize(h)) return 0;
    int s = api->db_open(short_name);
    if (s < 0 || api->db_exec(s, "CREATE TABLE t(i)") || api->db_close(s)) return 0;
    s = api->db_open(long_name);
    return s >= 0 && api->db_exec(s, "CREATE TABLE t(i)") == -1 &&
           !api->db_close(s) && !api->sys_stat(long_name, &st) && !st.st_size &&
           count_rows(h, 2);
}

static int fp_step(int h)
{
    const unsigned long long sentinel = 0x4045000012345678ULL;
    unsigned long long saved, after, value;
    __asm__ volatile("mffs 0\n stfd 0,%0\n mtfsfi 7,1\n lfd 31,%1"
                     : "=m"(saved) : "m"(sentinel) : "fr0", "fr31", "memory");
    int rc = api->db_step(h);
    __asm__ volatile("mffs 0\n stfd 0,%0\n stfd 31,%1\n lfd 0,%2\n mtfsf 255,0"
                     : "=m"(after), "=m"(value) : "m"(saved) : "fr0", "memory");
    return rc == DB_STATUS_ROW && (after & 3) == 1 && value == sentinel;
}

int main(int argc, char **argv, KernelAPI *kernel)
{
    api = kernel;
    u32 base = api->db_mem_used();
    put("DB-BASE "); number(base); put("\n");
    if (argc == 3 && !strcmp(argv[1], "2")) {
        OS32_Stat st;
        memset(&st, 0xa5, sizeof(st));
        CHECK("STAT", !api->sys_stat(path, &st) && st.st_size > 0);
        u32 first = 0;
        for (const char *p = argv[2]; *p; ++p) first = first * 10 + (*p - '0');
        if (first != base) { report("RECLAIM", 0); return 1; }
        int h = api->db_open_existing(path, 0);
        CHECK("REOPEN", h >= 0 && count_rows(h, 2));
        CHECK("RECLAIM", !api->db_close(h) && api->db_mem_used() == base);
        return 0;
    }
    if (argc != 2 || strcmp(argv[1], "1")) return 2;
    int h = api->db_open(path);
    CHECK("OPEN", h >= 0);
    u32 used = api->db_mem_used();
    CHECK("CREATE", !api->db_exec(h, "CREATE TABLE t(i,a,b,x,n)"));
    CHECK("INSERT", !api->db_exec(h, "INSERT INTO t(i) VALUES(1)"));
    CHECK("BIND-RC", !api->db_prepare_only(h, "INSERT INTO t VALUES(?,?,?,?,?)") &&
          !api->db_bind_int(h, 1, 7) && !api->db_bind_text(h, 2, "alpha", 5) &&
          !api->db_bind_text(h, 3, "beta", 4) && !api->db_bind_blob(h, 4, "\x01\xff", 2) &&
          !api->db_bind_null(h, 5) && api->db_step(h) == DB_STATUS_DONE);
    CHECK("BIND-VAL", api->db_prepare(h, "SELECT i,a,b,hex(x),typeof(n) FROM t WHERE i=7") == DB_STATUS_ROW &&
          api->db_column_int(h, 0) == 7 && column(h, 1, "alpha") && column(h, 2, "beta") &&
          column(h, 3, "01FF") && column(h, 4, "null"));
    CHECK("STEP", !api->db_prepare_only(h, "SELECT i,a FROM t WHERE i=7") &&
          api->db_step(h) == DB_STATUS_ROW && api->db_column_int(h, 0) == 7 &&
          column(h, 1, "alpha") && api->db_step(h) == DB_STATUS_DONE);
    int conv_ok = conversion(h, "CONV-POS", "SELECT printf('%!.15g', 3000000000*1.0)", "3000000000.0", 0);
    conv_ok &= conversion(h, "CONV-NEG", "SELECT printf('%!.15g', -3000000000*1.0)", "-3000000000.0", 0);
    conv_ok &= conversion(h, "CONV-MOD-POS", "SELECT 3000000000.5%7", 0, 4);
    conv_ok &= conversion(h, "CONV-MOD-NEG", "SELECT -3000000000.5%7", 0, -4);
    conv_ok &= conversion(h, "CONV-SUM", "SELECT printf('%!.15g', 0.1+0.2)", "0.3", 0);
    if (!conv_ok) return 1;
    CHECK("FP-PRE", api->db_prepare(h, "SELECT 1.5") == DB_STATUS_ROW &&
          !api->db_prepare_only(h, "SELECT 2.5"));
    api->db_error_code(-2);
    int fp_ok = report("FPSCR", fp_step(h));
    int next_ok = report("NEXT", api->db_step(h) == DB_STATUS_DONE);
    if (!fp_ok || !next_ok) return 1;
    CHECK("ERR-RC", api->db_open("/X.DB") == -1 && api->db_exec(h, "broken SQL") == -1);
    const char *error = api->db_last_error(h);
    CHECK("ERR-CODE", error && *error && api->db_error_code(-1) == 14 && api->db_error_code(h) == 1);
    CHECK("NAME", names(h));
    CHECK("CLOSE", !api->db_close(h));
    CHECK("USED", used > base && api->db_mem_used() == base);
    h = api->db_open(path);
    CHECK("PENDING", h >= 0 && !api->db_exec(h, "BEGIN") &&
          !api->db_exec(h, "INSERT INTO t(i) VALUES(99)") &&
          api->db_prepare(h, "SELECT i FROM t") == DB_STATUS_ROW);
    return 0;
}
