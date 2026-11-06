/* H1-H4 の既存ケース用。製品と同じ SDK の型・生成定数で shell を供給する。
 * fd=1000 は贋 FS の FD 範囲外。FDRECOV のケースでは無効にして実ファイルを使う。
 * 本体ファイルの I/O カウンタ・障害注入はこの別の入力に消費させない。 */
#include "os32_generations.h"
#define FIXTURE_SHELL_FD 1000
static int fixture_shell_enabled = 1;
static u32 fixture_shell_pos;

static OS32Header fixture_shell_header(void)
{
    OS32Header h;
    memset(&h, 0, sizeof(h));
    h.magic = OS32X_MAGIC;
    h.header_size = OS32X_HDR_SIZE;
    h.version = OS32X_HDR_VERSION;
    h.kapi_data_off = KAPI_DATA_FIELDS_OFF;
    h.kapi_abi_generation = OS32_KAPI_ABI_GENERATION;
    h.memory_layout_generation = OS32_MEMORY_LAYOUT_GENERATION;
    h.shlib_protocol = 0;  /* 常駐 shell は shlib に依存しない。 */
    return h;
}

static int fixture_shell_read(void *buf, u32 size)
{
    OS32Header h = fixture_shell_header();
    u32 n = sizeof(h) - fixture_shell_pos;
    if (n > size) n = size;
    memcpy(buf, (u8 *)&h + fixture_shell_pos, n);
    fixture_shell_pos += n;
    return (int)n;
}

/* Existing sync cases use a readable settings DB with no custom row.
 * Stage L cases inject failures at this same KAPI boundary. */
static const char *fixture_user_path;
static int fixture_settings_fault, fixture_settings_step, fixture_settings_schema;
static char fixture_settings_path[OS32_MAX_PATH];
static int fixture_db_open(const char *path, int writable)
{
    int i;
    for (i = 0; path[i] && i < OS32_MAX_PATH - 1; i++) fixture_settings_path[i] = path[i];
    fixture_settings_path[i] = 0;
    fixture_settings_step = 0;
    return writable || fixture_settings_fault == 1 ? -1 : 1;
}
static int fixture_db_prepare(int h, const char *sql)
{
    (void)h;
    fixture_settings_schema = strstr(sql, "sqlite_master") != 0;
    fixture_settings_step = 0;
    return fixture_settings_fault == 2 ||
        (fixture_settings_fault == 9 && !fixture_settings_schema) ? -1 : 0;
}
static int fixture_db_step(int h)
{
    (void)h;
    if (fixture_settings_fault == 3 ||
        (fixture_settings_fault == 10 && !fixture_settings_schema)) return DB_STATUS_ERROR;
    if (fixture_settings_schema)
        return fixture_settings_fault != 8 && !fixture_settings_step++ ?
            DB_STATUS_ROW : DB_STATUS_DONE;
    return fixture_user_path && !fixture_settings_step++ ? DB_STATUS_ROW : DB_STATUS_DONE;
}
static int fixture_db_int(int h, int col)
{
    (void)h;
    if (!col) return fixture_settings_fault == 4 ? 0 : 1;
    return (int)strlen(fixture_user_path) + (fixture_settings_fault == 7);
}
static const char *fixture_db_text(int h, int col)
{ (void)h; (void)col; return fixture_user_path; }
static int fixture_db_finalize(int h)
{ (void)h; return fixture_settings_fault == 5 ? -1 : 0; }
static int fixture_db_close(int h)
{ (void)h; return fixture_settings_fault == 6 ? -1 : 0; }
static void fixture_settings_init(KernelAPI *table)
{
    fixture_user_path = 0; fixture_settings_fault = 0;
    table->db_open_existing = fixture_db_open;
    table->db_prepare_only = fixture_db_prepare;
    table->db_step = fixture_db_step;
    table->db_column_int = fixture_db_int;
    table->db_column_text = fixture_db_text;
    table->db_finalize = fixture_db_finalize;
    table->db_close = fixture_db_close;
}
