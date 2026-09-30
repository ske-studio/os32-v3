/* ======================================================================== */
/*  IME.H — OS32 カーネルFEP (日本語入力システム)                            */
/*                                                                          */
/*  カーネル常駐型の日本語入力フロントエンドプロセッサ。                      */
/*  辞書検索エンジン・ローマ字かな変換・プリエディット表示を統合する。        */
/* ======================================================================== */

#ifndef __IME_H
#define __IME_H

#include "types.h"
#include "os32_kapi_shared.h"
#include "ime_render.h"

/* ======================================================================== */
/*  IME 状態                                                                */
/* ======================================================================== */

#define IME_ST_INPUT       0    /* かな入力中 (未変換) */
#define IME_ST_CONVERT     1    /* 変換候補選択中 (インライン▼表示) */
#define IME_ST_CANDLIST    2    /* 候補リストウィンドウ展開中 */

/* ======================================================================== */
/*  辞書検索結果                                                             */
/* ======================================================================== */

/* 検索結果の最大数 */
#define IME_MAX_RESULTS     32

/* 検索結果エントリ */
typedef struct {
    char yomi[32];      /* 読み (UTF-8, ヌル終端) */
    char kanji[32];     /* 漢字/表層形 (UTF-8, ヌル終端) */
    u16  pos_id;        /* 品詞ID */
    u16  cost;          /* コスト (小さいほど優先) */
} IME_Result;

/* ======================================================================== */
/*  辞書コンテキスト (SQLite ベース)                                         */
/* ======================================================================== */

typedef struct {
    void *db;            /* sqlite3* (前方宣言回避のため void*) */
    void *exact_stmt;    /* sqlite3_stmt* (完全一致検索) */
    void *prefix_stmt;   /* sqlite3_stmt* (前方一致検索) */
    void *learn_stmt;    /* sqlite3_stmt* (学習UPSERT) */
    /* 常駐接続の開き直し (票 TASK_VFS_FD_PATH 方針 v3 の 6)。開いた名前と、
     * FD の失効を伴わない I/O エラーで開き直した印 (成功した検索で戻す) */
    char path[OS32_MAX_PATH];
    int  io_retried;
} IME_Dict;

/* ======================================================================== */
/*  IME 状態構造体                                                           */
/* ======================================================================== */

/* ローマ字かな変換バッファ */
typedef struct {
    char preedit[8];    /* 未確定ローマ字バッファ (例: "ky") */
    char output[32];    /* 確定したかな出力 */
    int  n_wait;        /* 'n' 待ち状態フラグ */
} IME_RomKana;

/* IME全体状態 */
typedef struct {
    int         mode;           /* IME_MODE_xxx */
    IME_RomKana rk;             /* ローマ字かな変換 */
    char        kana_buf[128];  /* 入力中のかな列 (変換対象) */
    int         kana_len;       /* kana_bufのバイト長 */
    /* 変換候補 */
    IME_Result  results[IME_MAX_RESULTS];
    int         result_count;   /* ヒット数 */
    int         candidate_idx;  /* 選択中の候補インデックス */
    int         converting;     /* 1=変換候補表示中 */
    int         convert_len;    /* 変換対象の読みバイト数(最長一致用) */
    /* 確定済み出力バッファ */
    char        commit_buf[256];
    int         commit_pos;     /* 次に返すバイト位置 */
    int         commit_len;     /* 確定文字列のバイト長 */
    /* 辞書 */
    IME_Dict    dict;
    int         dict_loaded;    /* 辞書ロード済みフラグ */
    /* 描画抽象化 & 候補リスト制御 */
    int         state;          /* IME_ST_xxx */
    int         page;           /* 候補リストの現在ページ */
    int         per_page;       /* 1ページの候補数 */
    const IME_Render *render;   /* 描画バックエンド */
} IME_State;


/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

/* 初期化 (ブート時に1回) */
void ime_init(void);

/* FEP ON/OFF トグル (初回呼び出し時に辞書ロード) */
void ime_toggle(void);
int  ime_feed_key(int keydata);     /* WM から打鍵を 1 件通す (GUI v1.1、K 依頼 1) */
void ime_set_render(void *table);  /* 描画バックエンド差し替え、NULL で TVRAM 版 (K 依頼 2) */

/* FEP 状態取得 */
int  ime_is_active(void);

/* FEP モード設定/取得 */
void ime_set_mode(int mode);
int  ime_get_mode(void);

/* FEP経由のブロッキング入力 (確定文字を1バイトずつ返す) */
int  ime_getchar(void);

/* FEP経由のノンブロッキング入力 */
int  ime_trygetchar(void);

/*
 * FEP経由のブロッキングキー入力 (kbd_getkey互換)
 * 戻り値: (scancode << 8) | ascii
 * IME確定文字は scancode=0x00 で1バイトずつ返す。
 * 制御キー (矢印, TAB等) はIMEを透過して生のkeydataを返す。
 */
int  ime_getkey(void);
int  ime_trygetkey(void);

/* 辞書管理API (ime.c) */
int  ime_switch_dict(int variant);
int  ime_user_list_facade(const char *yomi_prefix, void *out, int max);
int  ime_user_delete_facade(const char *yomi, const char *kanji);
int  ime_user_export_facade(const char *path);
int  ime_user_clear_facade(void);

/* ======================================================================== */
/*  内部関数 (ime_romkana.c, ime_dict.c から公開)                            */
/* ======================================================================== */

/* ローマ字かな変換 (ime_romkana.c) */
void ime_rk_init(IME_RomKana *rk);
int  ime_rk_append(IME_RomKana *rk, char c);
int  ime_rk_flush_n(IME_RomKana *rk);
void ime_hira_to_kata(char *utf8_str);

/* 辞書操作 (ime_dict.c) */
int  ime_dict_open(IME_Dict *dict, const char *path);
int  ime_dict_search(IME_Dict *dict, const char *yomi,
                     IME_Result *results, int max_results);
void ime_dict_learn(IME_Dict *dict, const char *yomi, const char *kanji);
int  ime_dict_reopen(IME_Dict *dict, const char *path);
int  ime_user_list(IME_Dict *dict, const char *yomi_prefix,
                   IME_UserEntry *out, int max);
int  ime_user_delete(IME_Dict *dict, const char *yomi, const char *kanji);
int  ime_user_export(IME_Dict *dict, const char *path);
int  ime_user_clear(IME_Dict *dict);

#endif /* __IME_H */
