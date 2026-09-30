/* ======================================================================== */
/*  LIBMD.H - Markdown パーサーライブラリ                                    */
/*                                                                          */
/*  Markdownテキストを構造化ノード配列に変換する。                           */
/*  GFX非依存。純粋なデータ変換のみ。                                       */
/*                                                                          */
/*  対応要素:                                                               */
/*    # H1, ## H2, ### H3, - リスト, ``` コードブロック,                    */
/*    --- 水平線, `inline code`, **bold**, | テーブル |,                  */
/*    > ブロック引用, 段落, 空行                                          */
/* ======================================================================== */

#ifndef LIBMD_H
#define LIBMD_H

/* ノードタイプ (1論理行 = 1ノード) */
typedef enum {
    MD_H1,            /* # 見出し */
    MD_H2,            /* ## 見出し */
    MD_H3,            /* ### 見出し */
    MD_PARAGRAPH,     /* 通常段落テキスト */
    MD_LIST_ITEM,     /* - リスト項目 */
    MD_CODE_BLOCK,    /* ``` コードブロック内の行 */
    MD_HRULE,         /* --- / *** 水平線 */
    MD_TABLE_ROW,     /* | col | col | テーブル行 */
    MD_BLOCKQUOTE,    /* > 引用 */
    MD_BLANK          /* 空行 */
} MdNodeType;

/* インライン装飾スパン */
typedef enum {
    MD_SPAN_TEXT,     /* 通常テキスト */
    MD_SPAN_CODE,     /* `inline code` */
    MD_SPAN_BOLD      /* **bold** */
} MdSpanType;

typedef struct {
    MdSpanType type;
    int start;         /* text[]内のバイトオフセット */
    int len;           /* バイト数 */
} MdSpan;

#define MD_MAX_SPANS  16
#define MD_MAX_NODES  2048
#define MD_MAX_COLS   8    /* テーブルの最大列数 */

/* 1つのMarkdownノード */
typedef struct {
    MdNodeType  type;
    const char *text;       /* 表示テキスト (記法除去済み) */
    int         text_len;
    MdSpan      spans[MD_MAX_SPANS];
    int         span_count;
    /* テーブル用 */
    int         col_count;              /* 列数 (0 = テーブルでない) */
    const char *cols[MD_MAX_COLS];      /* 各セルの先頭ポインタ */
    int         col_lens[MD_MAX_COLS];  /* 各セルのバイト長 */
} MdNode;

/* パース済みドキュメント */
typedef struct {
    MdNode  nodes[MD_MAX_NODES];
    int     node_count;
} MdDocument;

/* ======================================================================== */
/*  API                                                                      */
/* ======================================================================== */

/*
 * md_parse - Markdownテキストをパースしてノード配列を構築する
 *
 * text_buf: 入力テキスト (破壊的に変更される: '\n' を '\0' に置換)
 * text_len: テキストのバイト長
 * 戻り値:   ノード数 (>= 0)、エラー時 -1
 */
int md_parse(MdDocument *doc, char *text_buf, int text_len);

#endif /* LIBMD_H */
