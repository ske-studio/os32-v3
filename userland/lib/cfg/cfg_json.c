/* ======================================================================== */
/*  CFG_JSON.C — `cfg export` が書く JSON の最小 reader (純関数、票 S3 §2)  */
/*                                                                          */
/*  汎用 JSON は持ち込まない (DESIGN §6b)。読むのは `userland/cmds/cfg.c`    */
/*  の export writer が出す形**だけ**:                                       */
/*                                                                          */
/*      {"schema_version":1,"exported":"7"}                                 */
/*      {"scope":"gshell","key":"desktop/color","type":0,"v":5}             */
/*      {"scope":"gshell","key":"a","type":1,"v":"x\ry"}                    */
/*      {"scope":"gshell","key":"b","type":2,"v":"AP8="}                    */
/*      {"scope":"gshell","key":"c","type":1,"v":null}                      */
/*                                                                          */
/*  キーの順は固定、空白は 1 バイトも許さない、余分なキーは拒否。            */
/*  エスケープは writer が出す 6 種だけ (`\"` `\\` `\n` `\r` `\t` `\uXXXX`)。*/
/*  `\u` は BMP のみ — 非 BMP を writer は生 UTF-8 で出すので、             */
/*  サロゲート (D800〜DFFF) は「writer が出さない形」として拒否する。        */
/*                                                                          */
/*  上限 (scope 63 / key 63 / text 255B / blob 4096B) と UTF-8 / NUL 禁止は  */
/*  **復号した後**に見る。UTF-8 / NUL の検査は文字列 (scope / key / text)    */
/*  だけ — blob は base64 を解いた生バイト列なので適用しない (票 §2)。       */
/* ======================================================================== */

#include "cfg_internal.h"

/* ------------------------------------------------------------------ */
/*  1 行の上を進む走査器                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    const char *p;
    int n;
    int i;
} JIn;

static int j_left(const JIn *in) { return in->n - in->i; }

static int j_peek(const JIn *in)
{
    if (in->i >= in->n) return -1;
    return (int)(unsigned char)in->p[in->i];
}

static int j_take(JIn *in)
{
    int c = j_peek(in);
    if (c >= 0) in->i++;
    return c;
}

/* 骨組みの字句をそのまま食う。0 = 一致 / -1。 */
static int j_lit(JIn *in, const char *s)
{
    int k;
    for (k = 0; s[k]; k++) {
        if (in->i >= in->n || in->p[in->i] != s[k]) return -1;
        in->i++;
    }
    return 0;
}

static int j_hex(int c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

/* ------------------------------------------------------------------ */
/*  文字列 (両端の " を含めて食う)                                     */
/* ------------------------------------------------------------------ */

/*  out は cap バイト (NUL 終端を含む)。cap-1 バイトを越えたら *over を立て、
 *  それ以上は書かずに区切りまで読み進める (長さの判定は呼び手が行う)。
 *  復号の結果 0 バイトが出たら *nul を立てる (`\u0000`)。
 *  戻り: CFG_JSON_OK / E_SYNTAX / E_UTF8 (サロゲート)。 */
static int j_string(JIn *in, char *out, int cap, int *len, int *over, int *nul)
{
    int c, d, k, h, v;
    int n = 0;
    unsigned char enc[4];
    int elen;

    *over = 0;
    *nul = 0;
    *len = 0;
    if (j_take(in) != '"') return CFG_JSON_E_SYNTAX;
    for (;;) {
        c = j_take(in);
        if (c < 0) return CFG_JSON_E_SYNTAX;          /* 閉じない */
        if (c == '"') break;
        elen = 0;
        if (c == '\\') {
            d = j_take(in);
            if (d == '"')       { enc[elen++] = '"'; }
            else if (d == '\\') { enc[elen++] = '\\'; }
            else if (d == 'n')  { enc[elen++] = '\n'; }
            else if (d == 'r')  { enc[elen++] = '\r'; }
            else if (d == 't')  { enc[elen++] = '\t'; }
            else if (d == 'u') {
                v = 0;
                for (k = 0; k < 4; k++) {
                    h = j_hex(j_take(in));
                    if (h < 0) return CFG_JSON_E_SYNTAX;
                    v = (v << 4) | h;
                }
                /* 非 BMP は writer が生 UTF-8 で出す = サロゲートは来ない。*/
                if (v >= 0xD800 && v <= 0xDFFF) return CFG_JSON_E_UTF8;
                if (v < 0x80) {
                    enc[elen++] = (unsigned char)v;
                } else if (v < 0x800) {
                    enc[elen++] = (unsigned char)(0xC0 | (v >> 6));
                    enc[elen++] = (unsigned char)(0x80 | (v & 0x3F));
                } else {
                    enc[elen++] = (unsigned char)(0xE0 | (v >> 12));
                    enc[elen++] = (unsigned char)(0x80 | ((v >> 6) & 0x3F));
                    enc[elen++] = (unsigned char)(0x80 | (v & 0x3F));
                }
            } else {
                return CFG_JSON_E_SYNTAX;             /* \b \f \/ 等は出さない */
            }
        } else if (c < 0x20) {
            return CFG_JSON_E_SYNTAX;                 /* 生の制御文字 */
        } else {
            enc[elen++] = (unsigned char)c;
        }
        for (k = 0; k < elen; k++) {
            if (enc[k] == 0) *nul = 1;
            if (n < cap - 1) {
                out[n] = (char)enc[k];
                n++;
            } else {
                /* 上限超過はカウンタを飽和させる (tsv reader と同じ、B5)。*/
                *over = 1;
            }
        }
    }
    out[n] = '\0';
    *len = n;
    return CFG_JSON_OK;
}

/* ------------------------------------------------------------------ */
/*  整数 (int32 の正準形だけ)                                          */
/* ------------------------------------------------------------------ */

/*  writer の fmt_int が出す形: 先頭ゼロ無し、`+` 無し、`-0` 無し。
 *  int32 の**範囲外は構文違反ではない**ので `*range` に控えるだけにし、
 *  数字は最後まで読み進める (対象外の scope の行を落とさないため)。 */
static int j_int(JIn *in, int *out, int *range)
{
    int c, neg = 0, digits = 0;
    u32 acc = 0, limit;

    *range = 0;
    if (j_peek(in) == '-') { neg = 1; in->i++; }
    limit = neg ? 2147483648UL : 2147483647UL;
    for (;;) {
        c = j_peek(in);
        if (c < '0' || c > '9') break;
        in->i++;
        if (digits == 1 && acc == 0) return CFG_JSON_E_VALUE;   /* 先頭ゼロ */
        digits++;
        if (*range || acc > (limit - (u32)(c - '0')) / 10UL) {
            *range = 1;                       /* 以後は溜めない (あふれ回避) */
        } else {
            acc = acc * 10UL + (u32)(c - '0');
        }
    }
    if (digits == 0) return CFG_JSON_E_VALUE;
    if (neg && acc == 0 && !*range) return CFG_JSON_E_VALUE;    /* -0 */
    /* INT_MIN の単項マイナスは signed overflow (UB)。表現できる値で組む。*/
    *out = neg ? -(int)(acc - 1UL) - 1 : (int)acc;
    return CFG_JSON_OK;
}

/* ------------------------------------------------------------------ */
/*  base64 (blob)                                                      */
/* ------------------------------------------------------------------ */

static int b64_val(int c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

/*  両端の " を含めて食い、復号したバイト列を out に置く。
 *  writer の fmt_b64 の出力だけを受ける: 4 文字単位、`=` は末尾の 1〜2 個
 *  だけ、英数字 + `+` `/` 以外は拒否 (`\` も来ない)。 */
static int j_b64(JIn *in, unsigned char *out, int cap, int *len, int *over,
                 int *noncanon)
{
    int c, v, pad = 0, quad = 0, n = 0;
    u32 acc = 0;

    *len = 0;
    *over = 0;
    *noncanon = 0;
    if (j_take(in) != '"') return CFG_JSON_E_SYNTAX;
    for (;;) {
        c = j_take(in);
        if (c < 0) return CFG_JSON_E_SYNTAX;
        if (c == '"') break;
        if (c == '=') {
            /* 詰めは最後の 4 文字組の中だけ、しかも 1〜2 個。 */
            if (quad < 2 || pad >= 2) return CFG_JSON_E_VALUE;
            pad++;
            acc <<= 6;
        } else {
            if (pad) return CFG_JSON_E_VALUE;        /* 詰めの後に本体 */
            v = b64_val(c);
            if (v < 0) return CFG_JSON_E_VALUE;
            acc = (acc << 6) | (u32)v;
        }
        quad++;
        if (quad == 4) {
            /* 捨てるバイトに落ちる**未使用ビットは 0** でなければならない。
             * `AB==` / `AAB=` は writer が出さない非正準形。ただしこれは
             * **意味**の傷で構文違反ではない — ここで返すと `--scope` の
             * 対象外の行でも import が落ちる (往復 2 の B3 残存)。
             * pad=1 は最後の実文字の下位 2bit、pad=2 は下位 4bit が余る。 */
            if (pad == 1 && ((acc >> 6) & 0x03UL) != 0) *noncanon = 1;
            if (pad == 2 && ((acc >> 12) & 0x0FUL) != 0) *noncanon = 1;
            /* 上限超過も**意味**の傷。形の検査は最後まで続ける。 */
            if (n + 3 - pad > cap) {
                *over = 1;
            } else {
                out[n++] = (unsigned char)((acc >> 16) & 0xFF);
                if (pad < 2) out[n++] = (unsigned char)((acc >> 8) & 0xFF);
                if (pad < 1) out[n++] = (unsigned char)(acc & 0xFF);
            }
            quad = 0;
            acc = 0;
            if (pad) {
                /* 詰めがあれば必ずそこが終わり。次は閉じ引用符。 */
                if (j_take(in) != '"') return CFG_JSON_E_VALUE;
                break;
            }
        }
    }
    if (quad != 0) return CFG_JSON_E_VALUE;          /* 4 の倍数でない */
    *len = n;
    return CFG_JSON_OK;
}

/* ------------------------------------------------------------------ */
/*  ヘッダ行                                                           */
/* ------------------------------------------------------------------ */

int cfg_json_header(const char *line, int len, int *version_out)
{
    JIn in;
    char stamp[64];
    int v = 0, slen, over, nul, rc, range = 0;

    if (version_out) *version_out = 0;
    if (!line || len < 0) return CFG_JSON_E_SYNTAX;
    in.p = line;
    in.n = len;
    in.i = 0;
    if (j_lit(&in, "{\"schema_version\":") != 0) return CFG_JSON_E_SYNTAX;
    rc = j_int(&in, &v, &range);
    if (rc != CFG_JSON_OK) return rc;
    /* ヘッダの版は import の入口。int32 に収まらない値は読めないので拒否。*/
    if (range) return CFG_JSON_E_RANGE;
    if (j_lit(&in, ",\"exported\":") != 0) return CFG_JSON_E_SYNTAX;
    rc = j_string(&in, stamp, (int)sizeof(stamp), &slen, &over, &nul);
    if (rc != CFG_JSON_OK) return rc;
    if (over || nul) return CFG_JSON_E_SYNTAX;
    if (j_lit(&in, "}") != 0) return CFG_JSON_E_SYNTAX;
    if (j_left(&in) != 0) return CFG_JSON_E_SYNTAX;
    if (version_out) *version_out = v;
    /* 版 0 以下は writer が出さない。自分より新しい版は拒否 (DESIGN §6b)。 */
    if (v < 1) return CFG_JSON_E_VALUE;
    if (v > CFG_SCHEMA_VERSION) return CFG_JSON_E_VERSION;
    return CFG_JSON_OK;
}

/* ------------------------------------------------------------------ */
/*  レコード行                                                         */
/* ------------------------------------------------------------------ */

int cfg_json_record(const char *line, int len, CfgJsonRow *row)
{
    JIn in;
    int rc, c;

    if (!line || len < 0 || !row) return CFG_JSON_E_SYNTAX;
    row->scope[0] = '\0';
    row->slen = 0;
    row->key[0] = '\0';
    row->klen = 0;
    row->type = -1;
    row->is_null = 0;
    row->ival = 0;
    row->tval[0] = '\0';
    row->tlen = 0;
    row->blen = 0;
    row->scope_over = 0;
    row->scope_nul = 0;
    row->key_over = 0;
    row->key_nul = 0;
    row->val_over = 0;
    row->val_nul = 0;
    row->val_range = 0;
    row->val_b64 = 0;
    in.p = line;
    in.n = len;
    in.i = 0;

    if (j_lit(&in, "{\"scope\":") != 0) return CFG_JSON_E_SYNTAX;
    rc = j_string(&in, row->scope, CFG_SCOPE_MAX + 1, &row->slen,
                  &row->scope_over, &row->scope_nul);
    if (rc != CFG_JSON_OK) return rc;

    if (j_lit(&in, ",\"key\":") != 0) return CFG_JSON_E_SYNTAX;
    rc = j_string(&in, row->key, CFG_KEY_MAX + 1, &row->klen,
                  &row->key_over, &row->key_nul);
    if (rc != CFG_JSON_OK) return rc;

    if (j_lit(&in, ",\"type\":") != 0) return CFG_JSON_E_SYNTAX;
    c = j_take(&in);
    /* 型は値の形を決めるので**構文**の一部。0/1/2 以外は値が解けない。 */
    if (c == '0') row->type = CFG_TYPE_INT;
    else if (c == '1') row->type = CFG_TYPE_TEXT;
    else if (c == '2') row->type = CFG_TYPE_BLOB;
    else return CFG_JSON_E_TYPE;

    if (j_lit(&in, ",\"v\":") != 0) return CFG_JSON_E_SYNTAX;
    if (j_peek(&in) == 'n') {
        if (j_lit(&in, "null") != 0) return CFG_JSON_E_VALUE;
        row->is_null = 1;
    } else if (row->type == CFG_TYPE_INT) {
        rc = j_int(&in, &row->ival, &row->val_range);
        if (rc != CFG_JSON_OK) return rc;
    } else if (row->type == CFG_TYPE_TEXT) {
        rc = j_string(&in, row->tval, CFG_TEXT_MAX + 1, &row->tlen,
                      &row->val_over, &row->val_nul);
        if (rc != CFG_JSON_OK) return rc;
    } else {
        /* blob は生バイト列。UTF-8 / NUL の検査は**しない** (票 §2)。 */
        rc = j_b64(&in, row->bval, CFG_BLOB_MAX, &row->blen, &row->val_over,
                   &row->val_b64);
        if (rc != CFG_JSON_OK) return rc;
    }

    if (j_lit(&in, "}") != 0) return CFG_JSON_E_SYNTAX;
    if (j_left(&in) != 0) return CFG_JSON_E_SYNTAX;
    return CFG_JSON_OK;
}

/* 切り詰めた scope を対象名と比べない。63B の対象名に前半が一致する長い
 * scope を「対象」と取り違えるため (レビュー往復 1 の B3)。 */
int cfg_json_scope_usable(const CfgJsonRow *row)
{
    if (!row) return 0;
    return !row->scope_over && !row->scope_nul;
}

/* 対象行にだけかける意味の検証。CFG_JSON_OK / CFG_JSON_E_*。 */
int cfg_json_check(const CfgJsonRow *row)
{
    if (!row) return CFG_JSON_E_SYNTAX;
    if (row->scope_nul) return CFG_JSON_E_NUL;
    if (row->scope_over || !cfg_i_valid_scope(row->scope))
        return CFG_JSON_E_SCOPE;
    if (cfg_i_utf8_check(row->scope, row->slen) != 0) return CFG_JSON_E_UTF8;
    if (row->key_nul) return CFG_JSON_E_NUL;
    if (row->key_over || !cfg_i_valid_key(row->key)) return CFG_JSON_E_KEY;
    if (cfg_i_utf8_check(row->key, row->klen) != 0) return CFG_JSON_E_UTF8;
    if (row->is_null) return CFG_JSON_OK;
    if (row->type == CFG_TYPE_INT) {
        if (row->val_range) return CFG_JSON_E_RANGE;
        return CFG_JSON_OK;
    }
    if (row->type == CFG_TYPE_TEXT) {
        if (row->val_nul) return CFG_JSON_E_NUL;
        if (row->val_over) return CFG_JSON_E_VALUE;
        if (cfg_i_utf8_check(row->tval, row->tlen) != 0)
            return CFG_JSON_E_UTF8;
        return CFG_JSON_OK;
    }
    /* blob: 4096B 超過と base64 の非正準形 (`AB==` / `AAB=`) */
    if (row->val_over || row->val_b64) return CFG_JSON_E_VALUE;
    return CFG_JSON_OK;
}
