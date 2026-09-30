/* ======================================================================== */
/*  MD_PARSE.C - Markdownパーサー実装                                        */
/*                                                                          */
/*  Markdownテキストを行単位で解析し、MdNode配列に変換する。                 */
/*  GFXに一切依存しない純粋なテキスト処理。                                 */
/* ======================================================================== */

#include "libos32md.h"

/* ======================================================================== */
/*  ユーティリティ                                                          */
/* ======================================================================== */

static int md_strlen(const char *s)
{
    int n = 0;
    while (*s++) n++;
    return n;
}

/* 行頭のスペース/タブをスキップして先頭位置を返す */
static const char *skip_spaces(const char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

/* 文字列が n文字以上の同一文字 ch で構成されているか */
static int is_repeated_char(const char *s, char ch, int min_count)
{
    int count = 0;
    while (*s == ch) { count++; s++; }
    /* 末尾の空白は許容 */
    while (*s == ' ' || *s == '\t') s++;
    return (*s == '\0' && count >= min_count);
}

/* ======================================================================== */
/*  インラインスパン分解                                                    */
/* ======================================================================== */

/*
 * テキスト内の `code` と **bold** を検出してスパン配列に分解する。
 * text は記法込みの文字列、out_spans に結果を格納。
 * 戻り値: スパン数
 *
 * 仕様:
 *   - ` (バッククォート) で囲まれた部分 → MD_SPAN_CODE
 *   - ** で囲まれた部分 → MD_SPAN_BOLD
 *   - それ以外 → MD_SPAN_TEXT
 *   - 記法文字自体はスパンに含めない (startは記法の次の文字)
 */
static int parse_inline_spans(const char *text, int text_len,
                              MdSpan *out_spans, int max_spans)
{
    int pos = 0;
    int span_count = 0;
    int seg_start = 0;

    while (pos < text_len && span_count < max_spans - 1) {
        /* バッククォート検出 */
        if (text[pos] == '`') {
            /* 現在のテキストスパンを閉じる */
            if (pos > seg_start) {
                out_spans[span_count].type = MD_SPAN_TEXT;
                out_spans[span_count].start = seg_start;
                out_spans[span_count].len = pos - seg_start;
                span_count++;
                if (span_count >= max_spans) break;
            }
            /* コードスパンの開始 */
            pos++; /* ` をスキップ */
            seg_start = pos;
            while (pos < text_len && text[pos] != '`') pos++;
            out_spans[span_count].type = MD_SPAN_CODE;
            out_spans[span_count].start = seg_start;
            out_spans[span_count].len = pos - seg_start;
            span_count++;
            if (pos < text_len) pos++; /* 閉じ ` をスキップ */
            seg_start = pos;
            continue;
        }

        /* ** (太字) 検出 */
        if (text[pos] == '*' && pos + 1 < text_len && text[pos + 1] == '*') {
            /* 現在のテキストスパンを閉じる */
            if (pos > seg_start) {
                out_spans[span_count].type = MD_SPAN_TEXT;
                out_spans[span_count].start = seg_start;
                out_spans[span_count].len = pos - seg_start;
                span_count++;
                if (span_count >= max_spans) break;
            }
            /* 太字スパンの開始 */
            pos += 2; /* ** をスキップ */
            seg_start = pos;
            while (pos < text_len) {
                if (text[pos] == '*' && pos + 1 < text_len && text[pos + 1] == '*') break;
                pos++;
            }
            out_spans[span_count].type = MD_SPAN_BOLD;
            out_spans[span_count].start = seg_start;
            out_spans[span_count].len = pos - seg_start;
            span_count++;
            if (pos + 1 < text_len) pos += 2; /* 閉じ ** をスキップ */
            seg_start = pos;
            continue;
        }

        pos++;
    }

    /* 残りのテキストをスパンに */
    if (seg_start < text_len && span_count < max_spans) {
        out_spans[span_count].type = MD_SPAN_TEXT;
        out_spans[span_count].start = seg_start;
        out_spans[span_count].len = text_len - seg_start;
        span_count++;
    }

    return span_count;
}

/* ======================================================================== */
/*  行分類                                                                  */
/* ======================================================================== */

/* 行の先頭パターンから行タイプを判定し、表示テキストの先頭位置を返す */
static MdNodeType classify_line(const char *line, const char **out_text)
{
    const char *p = line;

    /* 空行チェック */
    if (*p == '\0') {
        *out_text = p;
        return MD_BLANK;
    }

    /* 見出し: # ## ### */
    if (p[0] == '#') {
        if (p[1] == '#') {
            if (p[2] == '#' && (p[3] == ' ' || p[3] == '\t')) {
                *out_text = skip_spaces(p + 4);
                return MD_H3;
            }
            if (p[2] == ' ' || p[2] == '\t') {
                *out_text = skip_spaces(p + 3);
                return MD_H2;
            }
        }
        if (p[1] == ' ' || p[1] == '\t') {
            *out_text = skip_spaces(p + 2);
            return MD_H1;
        }
    }

    /* リスト: "- " */
    if (p[0] == '-' && p[1] == ' ') {
        *out_text = p + 2;
        return MD_LIST_ITEM;
    }

    /* ブロック引用: "> " */
    if (p[0] == '>' && (p[1] == ' ' || p[1] == '\0')) {
        *out_text = (p[1] == ' ') ? p + 2 : p + 1;
        return MD_BLOCKQUOTE;
    }

    /* テーブル行: "|" で始まる */
    if (p[0] == '|') {
        *out_text = p;
        return MD_TABLE_ROW;
    }

    /* 水平線: "---" / "***" / "___" (3文字以上) */
    if (is_repeated_char(p, '-', 3) ||
        is_repeated_char(p, '*', 3) ||
        is_repeated_char(p, '_', 3)) {
        *out_text = p;
        return MD_HRULE;
    }

    /* 通常テキスト */
    *out_text = p;
    return MD_PARAGRAPH;
}

/* ======================================================================== */
/*  テーブル行パーサー                                                        */
/* ======================================================================== */

/* テーブル区切り線行かどうか判定 (|---|---| or |:---:|) */
static int is_table_separator(const char *line)
{
    const char *p = line;
    int has_dash = 0;

    if (*p != '|') return 0;
    p++;
    while (*p) {
        if (*p == '-' || *p == ':') has_dash = 1;
        else if (*p == '|') { /* OK */ }
        else if (*p == ' ' || *p == '\t') { /* OK */ }
        else return 0; /* その他の文字 → データ行 */
        p++;
    }
    return has_dash;
}

/* テーブル行のセルをパース */
static int parse_table_cells(const char *line, const char **cols,
                             int *col_lens, int max_cols)
{
    const char *p = line;
    int count = 0;

    /* 先頭の | をスキップ */
    if (*p == '|') p++;

    while (*p && count < max_cols) {
        const char *cell_start;
        const char *cell_end;

        /* セル先頭の空白をスキップ */
        while (*p == ' ' || *p == '\t') p++;
        cell_start = p;

        /* 次の | まで */
        while (*p && *p != '|') p++;
        cell_end = p;

        /* 末尾の空白をトリム */
        while (cell_end > cell_start &&
               (cell_end[-1] == ' ' || cell_end[-1] == '\t')) {
            cell_end--;
        }

        if (cell_end > cell_start || *p == '|') {
            cols[count] = cell_start;
            col_lens[count] = (int)(cell_end - cell_start);
            count++;
        }

        if (*p == '|') p++;
    }

    return count;
}

/* ======================================================================== */
/*  メインパーサー                                                          */
/* ======================================================================== */

int md_parse(MdDocument *doc, char *text_buf, int text_len)
{
    int in_code_block = 0;
    char *line_start;
    char *p;
    char *end;

    doc->node_count = 0;

    if (!text_buf || text_len <= 0) return 0;

    /* CR を除去 */
    p = text_buf;
    end = text_buf + text_len;
    for (; p < end; p++) {
        if (*p == '\r') *p = '\0';
    }

    /* 行ごとに処理 */
    line_start = text_buf;
    p = text_buf;

    while (p <= end && doc->node_count < MD_MAX_NODES) {
        /* 行末を探す (または末尾に到達) */
        if (p < end && *p != '\n') {
            p++;
            continue;
        }

        /* '\n' を '\0' に置換して行を確定 */
        if (p < end) *p = '\0';

        /* コードブロック ``` のトグル */
        if (line_start[0] == '`' && line_start[1] == '`' && line_start[2] == '`') {
            in_code_block = !in_code_block;
            /* ``` 行自体はノードに含めない → 次の行へ */
            p++;
            line_start = p;
            continue;
        }

        {
            MdNode *node = &doc->nodes[doc->node_count];
            const char *display_text;

            /* テーブル用フィールド初期化 */
            node->col_count = 0;

            if (in_code_block) {
                /* コードブロック内: そのまま */
                node->type = MD_CODE_BLOCK;
                node->text = line_start;
                node->text_len = md_strlen(line_start);
                node->span_count = 1;
                node->spans[0].type = MD_SPAN_TEXT;
                node->spans[0].start = 0;
                node->spans[0].len = node->text_len;
            } else {
                /* 行分類 */
                node->type = classify_line(line_start, &display_text);
                node->text = display_text;
                node->text_len = md_strlen(display_text);

                if (node->type == MD_TABLE_ROW) {
                    /* テーブル区切り線はスキップ */
                    if (is_table_separator(line_start)) {
                        p++;
                        line_start = p;
                        continue;
                    }
                    /* テーブルセルをパース */
                    node->col_count = parse_table_cells(
                        line_start, node->cols,
                        node->col_lens, MD_MAX_COLS);
                    /* スパン分解なし */
                    node->span_count = 0;

                } else if (node->type == MD_BLOCKQUOTE) {
                    /* ブロック引用: インラインスパン分解 */
                    node->span_count = parse_inline_spans(
                        node->text, node->text_len,
                        node->spans, MD_MAX_SPANS);

                } else if (node->type == MD_PARAGRAPH ||
                           node->type == MD_LIST_ITEM ||
                           node->type == MD_H3) {
                    /* インラインスパン分解 (段落・リスト・H3) */
                    node->span_count = parse_inline_spans(
                        node->text, node->text_len,
                        node->spans, MD_MAX_SPANS);
                } else if (node->type == MD_H1 || node->type == MD_H2) {
                    /* H1/H2はスパン分解なし (全体が見出しテキスト) */
                    node->span_count = 1;
                    node->spans[0].type = MD_SPAN_TEXT;
                    node->spans[0].start = 0;
                    node->spans[0].len = node->text_len;
                } else {
                    /* HRULE/BLANK */
                    node->span_count = 0;
                }
            }

            doc->node_count++;
        }

        p++;
        line_start = p;
    }

    return doc->node_count;
}
