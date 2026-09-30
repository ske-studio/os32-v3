/* ======================================================================== */
/*  LIBOS32DB.C — OS32 SQLite ユーザー空間ラッパー実装                       */
/*                                                                          */
/*  KAPI 関数テーブル経由でカーネル内 SQLite を操作する。                      */
/*  結果データは共有メモリ (MEM_SHM_BASE) 上の DB_ResultHeader +              */
/*  DB_ColumnInfo[] + データ を直接参照する。                                  */
/* ======================================================================== */

#include "libos32db.h"

/* API テーブルへのポインタ (crt0_c.c で定義される) */
extern KernelAPI *kapi;
#define api kapi

/* 共有メモリベースアドレス。
 * MEM_SHM_BASE はカーネルの __bss_end 由来で可変のため、アドレスを
 * ハードコードしてはならない (カーネルが肥大すると即座にズレる)。
 * KAPI v34 以降は kapi->shm_base から実行時に取得する。 */
#define DB_SHM_PTR   ((u8 *)api->shm_base)

/* ======================================================================== */
/*  接続管理                                                                 */
/* ======================================================================== */

db_handle_t db_open(const char *path)
{
    return api->db_open(path);
}

int db_close(db_handle_t h)
{
    return api->db_close(h);
}

/* ======================================================================== */
/*  SQL 実行                                                                 */
/* ======================================================================== */

int db_exec(db_handle_t h, const char *sql)
{
    return api->db_exec(h, sql);
}

int db_query(db_handle_t h, const char *sql)
{
    return api->db_prepare(h, sql);
}

int db_step(db_handle_t h)
{
    return api->db_step(h);
}

int db_finalize(db_handle_t h)
{
    return api->db_finalize(h);
}

/* ======================================================================== */
/*  カラムアクセサ — 共有メモリを直接参照                                    */
/* ======================================================================== */

int db_column_count(void)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    return (int)hdr->column_count;
}

int db_column_type(int col)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;
    if (col < 0 || col >= (int)hdr->column_count) return DB_TYPE_NULL;
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    return (int)info->type;
}

i32 db_column_int(int col)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;
    if (col < 0 || col >= (int)hdr->column_count) return 0;
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    if (info->type != DB_TYPE_INT || info->data_offset == 0) return 0;
    return *(i32 *)(DB_SHM_PTR + info->data_offset);
}

const char *db_column_text(int col)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;
    if (col < 0 || col >= (int)hdr->column_count) return "";
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    if (info->data_offset == 0) return "";
    return (const char *)(DB_SHM_PTR + info->data_offset);
}

int db_column_bytes(int col)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;
    if (col < 0 || col >= (int)hdr->column_count) return 0;
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    return (int)info->length;
}

const void *db_column_blob(int col)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    DB_ColumnInfo *info;
    if (col < 0 || col >= (int)hdr->column_count) return (const void *)0;
    info = (DB_ColumnInfo *)(DB_SHM_PTR + sizeof(DB_ResultHeader)
                             + (u32)col * sizeof(DB_ColumnInfo));
    if (info->data_offset == 0) return (const void *)0;
    return (const void *)(DB_SHM_PTR + info->data_offset);
}

/* ======================================================================== */
/*  エラー情報                                                               */
/* ======================================================================== */

const char *db_errmsg(void)
{
    DB_ResultHeader *hdr = (DB_ResultHeader *)DB_SHM_PTR;
    if (hdr->error_offset == 0) return "no error";
    return (const char *)(DB_SHM_PTR + hdr->error_offset);
}

const char *db_last_error(db_handle_t h)
{
    return api->db_last_error(h);
}

u32 db_mem_used(void)
{
    return api->db_mem_used();
}
