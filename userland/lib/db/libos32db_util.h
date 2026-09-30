/* ======================================================================== */
/*  LIBOS32DB_UTIL.H — DB系ライブラリ共通ユーティリティ                      */
/*                                                                          */
/*  libos32db を基盤とするドメイン特化ライブラリ (libos32ai, libos32battle   */
/*  等) が共通で使用するマクロおよびヘルパー関数を提供する。                  */
/*                                                                          */
/*  - DB_LOAD_TABLE / DB_LOAD_TABLE_OPT: テーブル→RAMキャッシュ読込         */
/*  - DB_FIND_BY_FIELD: 線形探索マクロ                                      */
/*  - db_sql_append / db_sql_append_int: SQL文字列組み立てヘルパー           */
/*                                                                          */
/*  依存: libos32db.h                                                       */
/*  C89 (GNU89) 互換: GCC statement expression ({...}) を使用               */
/* ======================================================================== */

#ifndef LIBOS32DB_UTIL_H
#define LIBOS32DB_UTIL_H

#include "libos32db.h"

/* ====================================================================== */
/*  1. テーブルローダーマクロ                                              */
/*                                                                          */
/*  DB_LOAD_TABLE — SELECTの結果を固定長配列にキャッシュする定型パターン。   */
/*                                                                          */
/*  引数:                                                                   */
/*    db_h   : db_handle_t                                                 */
/*    sql    : SQL文字列リテラル (SELECT文)                                 */
/*    array  : 格納先配列 (例: g_items)                                     */
/*    max    : 配列の最大要素数 (例: MAX_ITEMS)                             */
/*    count  : 格納済みカウンタ変数 (例: g_item_count)                      */
/*    BODY   : 1行分のフィールドマッピングコード。                          */
/*             `row` は &array[count] を指すポインタとして参照可能。         */
/*                                                                          */
/*  戻り値 (式全体): ロードした行数 (負値=エラー)                           */
/*                                                                          */
/*  使用例:                                                                 */
/*    DB_LOAD_TABLE(g_db,                                                   */
/*        "SELECT id, type FROM items ORDER BY id",                         */
/*        g_items, MAX_ITEMS, g_item_count,                                 */
/*        {                                                                 */
/*            row->id   = (u16)db_column_int(0);                            */
/*            row->type = (u8)db_column_int(1);                             */
/*        });                                                               */
/* ====================================================================== */

#define DB_LOAD_TABLE(db_h, sql, array, max, count, BODY)                   \
    ({                                                                      \
        int _rc, _cnt = 0;                                                  \
        _rc = db_query((db_h), (sql));                                      \
        if (_rc < 0) {                                                      \
            _cnt = -1;                                                      \
        } else {                                                            \
            while (_rc == DB_STATUS_ROW && _cnt < (int)(max)) {             \
                typeof((array)[0]) *row = &(array)[_cnt];                   \
                BODY                                                        \
                _cnt++;                                                     \
                _rc = db_step((db_h));                                      \
            }                                                               \
            (count) = _cnt;                                                 \
        }                                                                   \
        _cnt;                                                               \
    })

/* ====================================================================== */
/*  DB_LOAD_TABLE_OPT — テーブルが存在しなくてもエラーにしないバージョン    */
/*                                                                          */
/*  レシピ、セットボーナス等のオプショナルテーブル用。                       */
/*  db_query が失敗しても 0 を返し、処理を継続する。                        */
/* ====================================================================== */

#define DB_LOAD_TABLE_OPT(db_h, sql, array, max, count, BODY)               \
    ({                                                                      \
        int _rc, _cnt = 0;                                                  \
        _rc = db_query((db_h), (sql));                                      \
        if (_rc >= 0) {                                                     \
            while (_rc == DB_STATUS_ROW && _cnt < (int)(max)) {             \
                typeof((array)[0]) *row = &(array)[_cnt];                   \
                BODY                                                        \
                _cnt++;                                                     \
                _rc = db_step((db_h));                                      \
            }                                                               \
        }                                                                   \
        (count) = _cnt;                                                     \
        _cnt;                                                               \
    })

/* ====================================================================== */
/*  2. 線形探索マクロ                                                      */
/*                                                                          */
/*  DB_FIND_BY_FIELD — 固定長配列から指定フィールドが一致する要素を探す。   */
/*                                                                          */
/*  引数:                                                                   */
/*    array : 探索対象の配列                                                */
/*    count : 有効要素数                                                    */
/*    field : 比較対象の構造体フィールド名 (例: id)                         */
/*    value : 検索する値                                                    */
/*                                                                          */
/*  戻り値: インデックス (見つからない場合 -1)                              */
/*                                                                          */
/*  使用例:                                                                 */
/*    int idx = DB_FIND_BY_FIELD(g_items, g_item_count, id, item_id);       */
/* ====================================================================== */

#define DB_FIND_BY_FIELD(array, count, field, value)                         \
    ({                                                                      \
        int _i, _result = -1;                                               \
        for (_i = 0; _i < (int)(count); _i++) {                             \
            if ((array)[_i].field == (value)) {                             \
                _result = _i;                                               \
                break;                                                      \
            }                                                               \
        }                                                                   \
        _result;                                                            \
    })

/* ====================================================================== */
/*  3. SQL文字列組み立てヘルパー                                           */
/*                                                                          */
/*  snprintf を使わずに SQL 文字列を構築するためのユーティリティ。           */
/*  chem_core.c / map_core.c / text_core.c 等で個別実装されていた            */
/*  u16_to_str() / str_append() を統一する。                                */
/* ====================================================================== */

/*
 * db_sql_append — 文字列 src を *dst の位置に追記し、*dst を進める。
 *
 * dst:  書き込み先ポインタのアドレス (呼び出し後、末尾を指す)
 * src:  追記する文字列 (NUL終端)
 */
static void __attribute__((unused)) db_sql_append(char **dst, const char *src)
{
    char *p = *dst;
    while (*src) {
        *p++ = *src++;
    }
    *p = '\0';
    *dst = p;
}

/*
 * db_sql_append_int — 整数値を10進ASCII文字列に変換して追記する。
 *
 * dst:  書き込み先ポインタのアドレス
 * val:  追記する整数値 (負値対応)
 *
 * 最大桁数: 11桁 (-2147483648) + NUL = 12バイト必要
 */
static void __attribute__((unused)) db_sql_append_int(char **dst, int val)
{
    char *p = *dst;
    char digits[12];
    int di = 0;
    unsigned int uv;

    if (val < 0) {
        *p++ = '-';
        /* -2147483648 対応: unsigned に変換してからネゲート */
        uv = (unsigned int)(-(val + 1)) + 1u;
    } else {
        uv = (unsigned int)val;
    }

    if (uv == 0) {
        digits[di++] = '0';
    } else {
        while (uv > 0) {
            digits[di++] = '0' + (char)(uv % 10);
            uv /= 10;
        }
    }
    /* 逆順で書き込み */
    while (di > 0) {
        *p++ = digits[--di];
    }
    *p = '\0';
    *dst = p;
}

#endif /* LIBOS32DB_UTIL_H */
