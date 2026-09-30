/* ======================================================================== */
/*  CFG_TSV.C — settings.tsv の最小 reader (純関数、票 §1-7d)               */
/*                                                                          */
/*  tools/mk_settings_db.py の parse_tsv と**同じ規則**で読む。判定が一致    */
/*  することは tools/tests/test_cfg.py が同じ fixture を両方へ通して固定する。*/
/*                                                                          */
/*  行長の上限を持たない: 1 バイトずつ引き取り、列ごとの上限 (scope 63 /     */
/*  key 63 / type 4 / text 255B / blob 8192 文字 / int は先頭ゼロを畳んで    */
/*  10 桁) を数えながら進む。コメント行は改行まで読み飛ばすが、**読み捨て    */
/*  ではない** — CR と不正 UTF-8 はコメントの中でも拒否する (生成ツールは    */
/*  ファイル全体を先に検査するので、同じ入力を同じ判定にするため)。          */
/* ======================================================================== */

#include "cfg_internal.h"

#define TSV_EOF   (-1)
#define TSV_IOERR (-2)

#define CFG_TSV_INT_DIGITS 10       /* 先頭ゼロを畳んだ後の桁数の上限 */

typedef struct {
    int (*get)(void *ctx);
    void *gctx;
    CfgU8 u8;
    int lineno;
    int pushed;       /* 1 バイトの押し戻し (無し = -3) */
    int err;
    int done;         /* EOF に届いた */
} TsvIn;

#define TSV_NONE (-3)

static void in_init(TsvIn *in, int (*get)(void *), void *gctx)
{
    in->get = get;
    in->gctx = gctx;
    cfg_i_u8_reset(&in->u8);
    in->lineno = 1;
    in->pushed = TSV_NONE;
    in->err = CFG_TSV_OK;
    in->done = 0;
}

/* 1 バイト取る。CR と不正 UTF-8 はここで捕まえる (行番号は現在行)。 */
static int in_next(TsvIn *in)
{
    int c;
    if (in->pushed != TSV_NONE) {
        c = in->pushed;
        in->pushed = TSV_NONE;
        return c;
    }
    if (in->done) return TSV_EOF;
    c = in->get(in->gctx);
    if (c == TSV_IOERR) { in->err = CFG_TSV_E_IO; return TSV_IOERR; }
    if (c < 0) {
        in->done = 1;
        if (!cfg_i_u8_done(&in->u8)) { in->err = CFG_TSV_E_UTF8; return TSV_IOERR; }
        return TSV_EOF;
    }
    c &= 0xFF;
    if (c == '\r') { in->err = CFG_TSV_E_CR; return TSV_IOERR; }
    if (cfg_i_u8_byte(&in->u8, c) != 0) { in->err = CFG_TSV_E_UTF8; return TSV_IOERR; }
    if (c == '\n') {
        if (!cfg_i_u8_done(&in->u8)) { in->err = CFG_TSV_E_UTF8; return TSV_IOERR; }
    }
    return c;
}

static void in_push(TsvIn *in, int c)
{
    in->pushed = c;
}

/* ------------------------------------------------------------------ */
/*  列の読み取り                                                       */
/* ------------------------------------------------------------------ */

/* 1 列を buf に取る。区切りは TAB か LF か EOF。
 * `cap` は NUL を含む buf の大きさ = 受理できる長さは cap-1 バイト。
 * 戻り: 終端に使った文字 ('\t' / '\n' / TSV_EOF)、障害は TSV_IOERR。
 * *len に長さ、*over に「上限超過」、*nul に「NUL を含む」を返す。
 * 超過しても buf は必ず NUL 終端する (呼び手が over を見る前に触っても
 * 領域の外を読まないため)。 */
static int read_field(TsvIn *in, char *buf, int cap, int *len, int *over, int *nul)
{
    int c, n = 0;
    *over = 0;
    *nul = 0;
    for (;;) {
        c = in_next(in);
        if (c == TSV_IOERR) return TSV_IOERR;
        if (c == TSV_EOF || c == '\t' || c == '\n') break;
        if (c == 0) *nul = 1;
        if (n < cap - 1) {
            buf[n] = (char)c;
            n++;
        } else {
            /* 上限を越えたらカウンタを **飽和** させる。数え続けると
             * 2GB 級の入力で `int` が溢れ、`buf[n]` が負の添字になる
             * (往復 3 の B5)。UTF-8 / CR の検査は区切りまで続ける。 */
            *over = 1;
        }
    }
    buf[n] = '\0';
    *len = n;
    return c;
}

/* int の値。先頭ゼロは畳みながら読む。 */
static int read_int_field(TsvIn *in, int *out, int *code)
{
    int c, neg = 0, digits = 0, seen = 0;
    u32 acc = 0, limit;

    *code = CFG_TSV_OK;
    c = in_next(in);
    if (c == TSV_IOERR) return TSV_IOERR;
    if (c == '-') {
        neg = 1;
        c = in_next(in);
        if (c == TSV_IOERR) return TSV_IOERR;
    }
    limit = neg ? 2147483648UL : 2147483647UL;
    while (c != TSV_EOF && c != '\t' && c != '\n') {
        if (c < '0' || c > '9') { *code = CFG_TSV_E_VALUE; return TSV_IOERR; }
        seen = 1;
        if (!(digits == 0 && c == '0')) {
            digits++;
            if (digits > CFG_TSV_INT_DIGITS) { *code = CFG_TSV_E_RANGE; return TSV_IOERR; }
            if (acc > (limit - (u32)(c - '0')) / 10UL) {
                *code = CFG_TSV_E_RANGE;
                return TSV_IOERR;
            }
            acc = acc * 10UL + (u32)(c - '0');
        }
        c = in_next(in);
        if (c == TSV_IOERR) return TSV_IOERR;
    }
    if (!seen) { *code = CFG_TSV_E_VALUE; return TSV_IOERR; }
    /* INT_MIN の単項マイナスは signed overflow (UB)。表現できる値だけで組む。*/
    *out = neg ? -(int)(acc - 1UL) - 1 : (int)acc;
    return c;
}

static int hex_digit(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static int read_blob_field(TsvIn *in, unsigned char *out, int *outlen, int *code)
{
    int c, hi = -1, n = 0, chars = 0, d;

    *code = CFG_TSV_OK;
    for (;;) {
        c = in_next(in);
        if (c == TSV_IOERR) return TSV_IOERR;
        if (c == TSV_EOF || c == '\t' || c == '\n') break;
        d = hex_digit(c);
        if (d < 0) { *code = CFG_TSV_E_VALUE; return TSV_IOERR; }
        chars++;
        if (chars > CFG_BLOB_MAX * 2) { *code = CFG_TSV_E_VALUE; return TSV_IOERR; }
        if (hi < 0) {
            hi = d;
        } else {
            out[n++] = (unsigned char)((hi << 4) | d);
            hi = -1;
        }
    }
    if (hi >= 0) { *code = CFG_TSV_E_VALUE; return TSV_IOERR; }  /* 奇数桁 */
    *outlen = n;
    return c;
}

/* ------------------------------------------------------------------ */
/*  本体                                                               */
/* ------------------------------------------------------------------ */

static int fail(CfgTsvErr *err, int code, int lineno)
{
    err->code = code;
    err->lineno = lineno;
    return -1;
}

int cfg_tsv_parse(int (*get)(void *ctx), void *gctx, CfgTsvRow *row,
                  int (*emit)(const CfgTsvRow *r, void *ctx), void *ectx,
                  CfgTsvErr *err)
{
    TsvIn in;
    char type[8];
    int c, len, over, nul, code, line;

    if (!get || !row || !err) return -1;
    err->code = CFG_TSV_OK;
    err->lineno = 0;
    in_init(&in, get, gctx);

    for (;;) {
        line = in.lineno;
        c = in_next(&in);
        if (c == TSV_IOERR) return fail(err, in.err, in.lineno);
        if (c == TSV_EOF) break;
        if (c == '\n') { in.lineno++; continue; }        /* 空行 */
        if (c == '#') {                                  /* コメント行 */
            for (;;) {
                c = in_next(&in);
                if (c == TSV_IOERR) return fail(err, in.err, in.lineno);
                if (c == TSV_EOF) break;
                if (c == '\n') break;
            }
            if (c == '\n') in.lineno++;
            if (c == TSV_EOF) break;
            continue;
        }
        in_push(&in, c);

        /* --- 1 列目: scope --- */
        c = read_field(&in, row->scope, CFG_SCOPE_MAX + 1, &len, &over, &nul);
        if (c == TSV_IOERR) return fail(err, in.err, in.lineno);
        if (c != '\t') return fail(err, CFG_TSV_E_COLS, line);
        if (nul) return fail(err, CFG_TSV_E_NUL, line);
        if (over || !cfg_i_valid_scope(row->scope))
            return fail(err, CFG_TSV_E_SCOPE, line);

        /* --- 2 列目: key --- */
        c = read_field(&in, row->key, CFG_KEY_MAX + 1, &len, &over, &nul);
        if (c == TSV_IOERR) return fail(err, in.err, in.lineno);
        if (c != '\t') return fail(err, CFG_TSV_E_COLS, line);
        if (nul) return fail(err, CFG_TSV_E_NUL, line);
        if (over || !cfg_i_valid_key(row->key))
            return fail(err, CFG_TSV_E_KEY, line);

        /* --- 3 列目: type --- */
        c = read_field(&in, type, (int)sizeof(type), &len, &over, &nul);
        if (c == TSV_IOERR) return fail(err, in.err, in.lineno);
        if (c != '\t') return fail(err, CFG_TSV_E_COLS, line);
        if (over) return fail(err, CFG_TSV_E_TYPE, line);
        row->type = -1;
        if (len == 3 && type[0] == 'i' && type[1] == 'n' && type[2] == 't')
            row->type = CFG_TYPE_INT;
        else if (len == 4 && type[0] == 't' && type[1] == 'e' &&
                 type[2] == 'x' && type[3] == 't')
            row->type = CFG_TYPE_TEXT;
        else if (len == 4 && type[0] == 'b' && type[1] == 'l' &&
                 type[2] == 'o' && type[3] == 'b')
            row->type = CFG_TYPE_BLOB;
        if (row->type < 0) return fail(err, CFG_TSV_E_TYPE, line);

        /* --- 4 列目: value --- */
        row->ival = 0;
        row->tlen = 0;
        row->tval[0] = '\0';
        row->blen = 0;
        if (row->type == CFG_TYPE_INT) {
            c = read_int_field(&in, &row->ival, &code);
            if (c == TSV_IOERR) {
                if (code != CFG_TSV_OK) return fail(err, code, line);
                return fail(err, in.err, in.lineno);
            }
        } else if (row->type == CFG_TYPE_TEXT) {
            c = read_field(&in, row->tval, CFG_TEXT_MAX + 1, &len, &over, &nul);
            if (c == TSV_IOERR) return fail(err, in.err, in.lineno);
            if (nul) return fail(err, CFG_TSV_E_NUL, line);
            if (over) return fail(err, CFG_TSV_E_VALUE, line);
            row->tlen = len;
        } else {
            c = read_blob_field(&in, row->bval, &row->blen, &code);
            if (c == TSV_IOERR) {
                if (code != CFG_TSV_OK) return fail(err, code, line);
                return fail(err, in.err, in.lineno);
            }
        }
        if (c == '\t') return fail(err, CFG_TSV_E_COLS, line);   /* 5 列目 */

        row->lineno = line;
        if (emit && emit(row, ectx) != 0) return fail(err, CFG_TSV_E_EMIT, line);

        if (c == '\n') { in.lineno++; continue; }
        break;                                    /* 改行の無い最終行 */
    }
    return 0;
}
