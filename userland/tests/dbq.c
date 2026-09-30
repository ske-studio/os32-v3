/* ======================================================================== */
/*  DBQ.C — battle.db ロード失敗の実機診断                                    */
/*                                                                          */
/*  game の load_enemies() が実機でだけ 0 件を返す問題を切り分けるため、      */
/*  同じクエリを段階的に実行して rc と SQLite のエラー文字列を出力する。      */
/*  使い捨ての診断ツール。原因が確定したら消してよい。                        */
/* ======================================================================== */

#include "os32api.h"
#include "libos32db.h"

extern KernelAPI *kapi;

static void probe(db_handle_t h, const char *label, const char *sql)
{
    int rc = db_query(h, sql);
    kapi->kprintf(0x07, "[%s] rc=%d cols=%d\n", label, rc, db_column_count());
    if (rc == DB_STATUS_ERROR) {
        kapi->kprintf(0x06, "  err: %s\n", db_last_error(h));
        return;
    }
    {
        int rows = 0;
        while (rc == DB_STATUS_ROW && rows < 3) {
            int i;
            int n = db_column_count();
            kapi->kprintf(0x07, "  ");
            for (i = 0; i < n && i < 5; i++) {
                if (db_column_type(i) == DB_TYPE_TEXT) {
                    kapi->kprintf(0x07, "[%s] ", db_column_text(i));
                } else {
                    kapi->kprintf(0x07, "%d ", (int)db_column_int(i));
                }
            }
            kapi->kprintf(0x07, "\n");
            rows++;
            rc = db_step(h);
        }
        if (rc == DB_STATUS_ROW) db_finalize(h);
    }
}

int main(int argc, char **argv, KernelAPI *k)
{
    const char *path = "/db/battle.db";
    db_handle_t h;

    (void)k;
    if (argc >= 2) path = argv[1];

    h = db_open(path);
    kapi->kprintf(0x07, "db_open(%s) = %d\n", path, (int)h);
    if (h < 0) return 1;

    probe(h, "master", "SELECT name FROM sqlite_master WHERE type='table'");
    probe(h, "count",  "SELECT count(*) FROM enemies");
    probe(h, "game",
          "SELECT id, name, stage, kind, max_hp, atk, def, spd, mag, "
          "elements, exp, gold, class_id FROM enemies ORDER BY id");

    db_close(h);
    return 0;
}
