/* ======================================================================== */
/*  FILER_DRAW.H - TVRAMファイラー描画ライブラリ                             */
/*                                                                          */
/*  cmd_filer.c (シェル内蔵ファイラ) の描画関数を分離したモジュール。        */
/*  PC-98 TVRAM直接書き込みによるファイルリスト表示を担当。                  */
/* ======================================================================== */

#ifndef FILER_DRAW_H
#define FILER_DRAW_H

#include "os32api.h"

/* ======================================================================== */
/*  画面レイアウト定数                                                      */
/* ======================================================================== */

#define FL_SCREEN_COLS      80
#define FL_SCREEN_ROWS      25

#define FL_HEADER_Y         0
#define FL_SEPARATOR_Y      1
#define FL_LIST_TOP_Y       2
#define FL_LIST_ROWS        21
#define FL_FOOTER_Y         23
#define FL_HELP_Y           24

/* マルチカラム */
#define FL_NUM_COLUMNS      3
#define FL_COL_WIDTH        26
#define FL_PAGE_ITEMS       (FL_NUM_COLUMNS * FL_LIST_ROWS)

/* TVRAM属性値 */
#define FL_ATTR_HEADER      0xA1
#define FL_ATTR_SEPARATOR   0x21
#define FL_ATTR_DIR         0xA1
#define FL_ATTR_EXE         0x81
#define FL_ATTR_FILE        0xE1
#define FL_ATTR_SELECTED    0xE5
#define FL_ATTR_DIR_SEL     0xA5
#define FL_ATTR_EXE_SEL     0x85
#define FL_ATTR_FOOTER      0xE1
#define FL_ATTR_HELP        0xC1
#define FL_ATTR_STATUS      0x81

/* ======================================================================== */
/*  データ構造 (cmd_filer.cと共有)                                           */
/* ======================================================================== */

#define FL_MAX_ENTRIES      256
#define FL_MAX_NAME_LEN     64
#define FL_MAX_PATH_LEN     256

typedef struct {
    char name[FL_MAX_NAME_LEN];
    u32  size;
    u8   is_dir;
    u8   is_exe;
} FL_Entry;

typedef struct {
    FL_Entry entries[FL_MAX_ENTRIES];
    int count;
    int cursor;
    int page_top;
    /* 表に載せられなかった件数 (名前が FL_MAX_NAME_LEN に収まらない /
     * FL_MAX_ENTRIES を超えた)。切った名前で別のファイルを起動しないよう
     * 載せずに数だけ報せる — sh_ls.inc の「(... N more)」と同じ作法。 */
    int dropped;
    char cwd[FL_MAX_PATH_LEN];
} FL_State;

/* ======================================================================== */
/*  描画API                                                                  */
/* ======================================================================== */

/* 初期化 (KernelAPIポインタ設定) */
void fldraw_init(KernelAPI *api);

/* 1行を指定属性でクリア */
void fldraw_clear_line(int y, u8 attr);

/* 文字列描画 */
void fldraw_str(int x, int y, const char *s, int max_len, u8 attr);

/* 数値描画 */
void fldraw_number(int x, int y, u32 val, u8 attr);

/* サイズ文字列描画 (KB/MB自動変換) */
void fldraw_size_str(int x, int y, u32 size, u8 attr);

/* ヘッダ行描画 */
void fldraw_header(const FL_State *st);

/* 区切り線描画 */
void fldraw_separator(void);

/* 個別エントリ描画 */
void fldraw_entry(int col_x, int y, const FL_Entry *e, int selected);

/* 指定インデックスのエントリを描画 */
void fldraw_entry_at(const FL_State *st, int idx, int selected);

/* ファイルリスト全体描画 */
void fldraw_list(const FL_State *st);

/* フッタ行描画 */
void fldraw_footer(const FL_State *st);

/* ヘルプ行描画 */
void fldraw_help(const FL_State *st);

/* 全画面描画 */
void fldraw_all(const FL_State *st);

/* カーソル移動時の部分再描画 */
void fldraw_cursor_update(const FL_State *st, int old_cursor);

/* ポップアップメッセージ表示 */
void fldraw_popup_message(const char *msg, u8 attr);

#endif /* FILER_DRAW_H */
