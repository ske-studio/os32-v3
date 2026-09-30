/* ======================================================================== */
/*  UTF8.C — UTF-8デコーダ / Unicode→JIS変換 / u32最適化プリミティブ         */
/*                                                                          */
/*  システム標準エンコーディング: UTF-8                                        */
/*                                                                          */
/*  変換フロー:                                                              */
/*    UTF-8バイト列 → Unicodeコードポイント → JIS/ANKコード → KCG描画       */
/*                                                                          */
/*  変換の計算量:                                                            */
/*    - UTF-8デコード: O(1) ビット演算のみ                                   */
/*    - ひらがな/カタカナ: O(1) オフセット計算                                */
/*    - 漢字テーブル検索: O(log2(3160)) ≈ 12回比較                           */
/*    - テーブル検索はKCG I/Oアクセス(500μs)の2.6%以下                       */
/*                                                                          */
/*  u32最適化:                                                               */
/*    - マルチバイト文字の読み出しを le32_rd(src) 一括読み出しに最適化        */
/*    - i386のアンアラインドu32読み出しペナルティは小さい                      */
/*    - 日本語3バイト文字で3回のバイト読み→1回のu32読みに削減                  */
/*    - パックの並び (先頭バイトが最下位) は CPU のバイト順ではなく           */
/*      utf8_pack32 / utf8_cmp32 の約束。だから le32_rd() を通す              */
/* ======================================================================== */

#include "utf8.h"
#include "memmap.h"
#include "endian_le.h"

/* Unicode→JIS 変換表 (/sys/unicode.bin) は **媒体の上の外部形式** —
 * u16 (LE) が 0x10000 語並んだものを、0x4A000 へ生のまま読み込んでいる
 * (kernel/kernel.c の vfs_read)。表の番地は 2 バイト整列なのでアラインの
 * 心配は無いが、`u16 *` で引くと BE の CPU で値が入れ替わる。だから
 * バイト列として持ち、引くときに le16_rd() でバイト順を当てる。 */
static const u8 *unicode_jis_table = (const u8 *)MEM_UNICODE_TABLE_BASE;

static u16 jis_table_lookup(u32 cp)
{
    return le16_rd(unicode_jis_table + cp * 2);
}

/* Unicode→JIS 変換表 (/sys/unicode.bin) が使える状態か。
 *   -1 未判定 (初回参照時に自己診断する)
 *    0 使えない
 *    1 使える
 *
 * 表は BSS ではなく生の物理アドレス (0x4A000) に置かれるので、ロードに
 * 失敗しても「ゼロ埋め」にはならず、前に居たデータがそのまま表として
 * 読まれる — 画面が意味不明な漢字で埋まり、原因が表のロード失敗だと
 * 気づけない。だから「読めるかどうか」ではなく「正しい表かどうか」を
 * 確かめる必要がある。
 *
 * このフラグは static なので、カーネルと各外部プログラムがそれぞれ別の
 * 実体を持つ。カーネルはロード直後に utf8_set_jis_table_ready(1) を呼ぶが、
 * 外部プログラムには対応する契機が無く、以前は全プログラムで漢字が出ない
 * ままだった (ゲームだけが自前で有効化していた)。表は物理アドレス固定で
 * 共有されているので、こちら側で内容を検証して自動的に判定する。 */
static int jis_table_ready = -1;

void utf8_set_jis_table_ready(int ready)
{
    jis_table_ready = ready ? 1 : 0;
}

/* 既知の (Unicode, JIS X 0208) 対応を数点照合して、0x4A000 に載っている
 * のが本当に変換表かを確かめる。1 点だけだと残骸が偶然一致しうるので
 * 離れた位置から複数取る。 */
static int jis_table_probe(void)
{
    static const u32 probe_cp[4]  = { 0x4E9C, 0x4E00, 0x5BFE, 0x9078 };
    static const u16 probe_jis[4] = { 0x3021, 0x306C, 0x4250, 0x412A };
    int i;

    for (i = 0; i < 4; i++) {
        if (jis_table_lookup(probe_cp[i]) != probe_jis[i]) return 0;
    }
    return 1;
}

static int jis_table_usable(void)
{
    if (jis_table_ready < 0) {
        jis_table_ready = jis_table_probe();
    }
    return jis_table_ready;
}

/* ======================================================================== */
/*  u32 最適化プリミティブ                                                   */
/* ======================================================================== */

/* UTF-8文字列の先頭1文字のバイト数を返す (1~4) */
int utf8_char_bytes(const u8 *p)
{
    if ((*p & 0x80) == 0)    return 1;
    if ((*p & 0xE0) == 0xC0) return 2;
    if ((*p & 0xF0) == 0xE0) return 3;
    if ((*p & 0xF8) == 0xF0) return 4;
    return 1; /* 不正バイトは1バイトとして扱う */
}

/*
 * UTF-8の先頭1文字をu32にパック (リトルエンディアン、ゼロパディング)
 *
 *   1B文字: le32_rd(p) & 0x000000FF
 *   2B文字: le32_rd(p) & 0x0000FFFF
 *   3B文字: le32_rd(p) & 0x00FFFFFF  (日本語のホットパス)
 *   4B文字: le32_rd(p)              (マスク不要)
 *
 * 例: 「か」(U+304B) = E3 81 8B
 *   → le32_rd(ptr) & 0x00FFFFFF = 0x008B81E3
 *
 * **並びは CPU のバイト順ではなく、この関数が決めている** —
 * 先頭バイトが最下位、という約束は utf8_cmp32 の bswap32 と対になっていて、
 * 比較の正しさがそこに乗っている。だから素の `*(const u32 *)p` ではなく
 * le32_rd() を通す (include/endian_le.h)。x86 では同じ 1 命令に畳まれる。
 *
 * 注意: 呼び出し側でバッファ末尾の4B読み出し境界をチェックすること
 *       (le32_rd も 4 バイトを読む。ここは以前と同じ約束)。
 */
u32 utf8_pack32(const u8 *p)
{
    u8 b0 = p[0];
    if (b0 < 0x80) {
        /* ASCII — u32読み出し不要 */
        return (u32)b0;
    }
    if ((b0 & 0xE0) == 0xC0) {
        return le32_rd(p) & 0x0000FFFFu;
    }
    if ((b0 & 0xF0) == 0xE0) {
        return le32_rd(p) & 0x00FFFFFFu;
    }
    /* 4バイトシーケンス */
    return le32_rd(p);
}

/*
 * パック済みu32文字の辞書順比較
 *
 * リトルエンディアンではu32の整数比較がバイト辞書順と一致しない。
 * bswapでビッグエンディアンに変換することで正しい辞書順となる。
 *
 * 例: 「み」(E3 81 BF) vs「む」(E3 82 80)
 *   LE: 0x00BF81E3 > 0x008082E3 → 逆転！
 *   bswap(0x00BF81E3) = 0xE381BF00
 *   bswap(0x008082E3) = 0xE3828000
 *   0xE381BF00 < 0xE3828000 → 正しい ✓
 */
int utf8_cmp32(u32 a, u32 b)
{
    u32 sa = utf8_bswap32(a);
    u32 sb = utf8_bswap32(b);
    if (sa < sb) return -1;
    if (sa > sb) return 1;
    return 0;
}

/* ======================================================================== */
/*  UTF-8デコード (u32一括読み出し最適化)                                     */
/*                                                                          */
/*  UTF-8エンコーディング:                                                   */
/*    1バイト: 0xxxxxxx                    → U+0000-U+007F                  */
/*    2バイト: 110xxxxx 10xxxxxx           → U+0080-U+07FF                  */
/*    3バイト: 1110xxxx 10xxxxxx 10xxxxxx  → U+0800-U+FFFF                  */
/*    4バイト: 11110xxx 10xxxxxx 10xxxxxx 10xxxxxx → U+10000+              */
/*                                                                          */
/*  最適化: マルチバイト文字は *(u32*)src を1回読み出し、ビットシフトで       */
/*         各バイトを抽出。3バイト日本語で3回→1回のメモリアクセスに削減。     */
/*                                                                          */
/*  安全性: 呼び出し元でバッファ末尾の4B境界を保護すること。                   */
/*         console_write()はremaining<4時にtmp[4]にコピーして呼び出す。       */
/* ======================================================================== */
utf8_decode_t utf8_decode(const u8 *src)
{
    utf8_decode_t result;
    u8 b0 = src[0];
    u8 b1, b2, b3;

    if (b0 < 0x80) {
        /* ASCII (1バイト) — u32読み出し不要 */
        result.codepoint = b0;
        result.bytes_used = 1;
        return result;
    }

    /* マルチバイト: バイト単位で読み、NUL で打ち切る。
     * 以前の u32 一括読みは文字列末尾の不完全シーケンスで NUL 終端の
     * 先 (次ページ) まで読みに行き、ページ境界で #PF になり得た。 */
    b1 = src[1];
    b2 = (b1 != 0) ? src[2] : 0;
    b3 = (b2 != 0) ? src[3] : 0;

    if ((b0 & 0xE0) == 0xC0) {
        /* 2バイトシーケンス */
        if ((b1 & 0xC0) == 0x80) {
            result.codepoint = ((u32)(b0 & 0x1F) << 6) | (b1 & 0x3F);
            result.bytes_used = 2;
        } else {
            result.codepoint = 0xFFFD;  /* 置換文字 */
            result.bytes_used = 1;
        }
    }
    else if ((b0 & 0xF0) == 0xE0) {
        /* 3バイトシーケンス (日本語はここ) */
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80) {
            result.codepoint = ((u32)(b0 & 0x0F) << 12) |
                               ((u32)(b1 & 0x3F) << 6)  |
                               (b2 & 0x3F);
            result.bytes_used = 3;
        } else {
            result.codepoint = 0xFFFD;
            result.bytes_used = 1;
        }
    }
    else if ((b0 & 0xF8) == 0xF0) {
        /* 4バイトシーケンス (絵文字等) */
        if ((b1 & 0xC0) == 0x80 && (b2 & 0xC0) == 0x80 && (b3 & 0xC0) == 0x80) {
            result.codepoint = ((u32)(b0 & 0x07) << 18) |
                               ((u32)(b1 & 0x3F) << 12) |
                               ((u32)(b2 & 0x3F) << 6)  |
                               (b3 & 0x3F);
            result.bytes_used = 4;
        } else {
            result.codepoint = 0xFFFD;
            result.bytes_used = 1;
        }
    }
    else {
        /* 不正バイト */
        result.codepoint = 0xFFFD;
        result.bytes_used = 1;
    }

    return result;
}

/* ======================================================================== */
/*  Unicode → ANKコード変換                                                 */
/*  ANK描画可能なら文字コードを返す、不可なら0                                */
/* ======================================================================== */
u8 unicode_to_ank(u32 cp)
{
    /* ASCII */
    if (cp >= 0x20 && cp <= 0x7E) {
        return (u8)cp;
    }

    /* 全角英数 → 半角ASCII にフォールバック */
    if (cp >= 0xFF01 && cp <= 0xFF5E) {
        return (u8)(cp - 0xFF01 + 0x21);
    }

    /* 半角カナ (U+FF61-U+FF9F → ANK 0xA1-0xDF) */
    if (cp >= 0xFF61 && cp <= 0xFF9F) {
        return (u8)(cp - 0xFF61 + 0xA1);
    }

    /* バックスラッシュ/円記号 */
    if (cp == 0x00A5) return 0x5C;  /* ¥ → \ */

    return 0;
}

/* ======================================================================== */
/*  Unicode → JIS変換 (O(1) テーブル参照)                                   */
/* ======================================================================== */
u16 unicode_to_jis(u32 cp)
{
    /* ひらがな: U+3041-U+3093 → JIS 0x2421-0x2473 */
    if (cp >= 0x3041 && cp <= 0x3093) {
        return (u16)(0x2421 + (cp - 0x3041));
    }

    /* カタカナ: U+30A1-U+30F6 → JIS 0x2521-0x2576 */
    if (cp >= 0x30A1 && cp <= 0x30F6) {
        return (u16)(0x2521 + (cp - 0x30A1));
    }

    /* BMP範囲外 → 変換不可 */
    if (cp > 0xFFFF) return 0;

    /* 表が無いなら引かない (未ロードのゴミを漢字として表示しない)。
       未判定なら内容を検証して自動的に決める。 */
    if (!jis_table_usable()) return 0;

    /* O(1) でテーブルから直接引く */
    return jis_table_lookup(cp);
}

/* ======================================================================== */
/*  UTF-8 文字列ヘルパー                                                     */
/* ======================================================================== */

/* UTF-8文字列の文字数を返す (バイト数ではなく文字数) */
int utf8_strlen(const u8 *s)
{
    int count = 0;
    while (*s) {
        s += utf8_char_bytes(s);
        count++;
    }
    return count;
}

/*
 * UTF-8文字列の末尾1文字を削除
 *
 * 継続バイト(10xxxxxx)を巻き戻し、先頭バイトの位置にヌル終端を書き込む。
 * 例: "あい" (E3 81 82 E3 81 84) → "あ" (E3 81 82 00)
 */
void utf8_delete_last(u8 *s)
{
    u8 *end;
    if (*s == '\0') return;

    /* 末尾を見つける */
    end = s;
    while (*end) end++;

    /* 継続バイト(10xxxxxx)を巻き戻して先頭バイトを見つける */
    end--;
    while (end > s && (*end & 0xC0) == 0x80) {
        end--;
    }
    *end = '\0';
}

/* UTF-8文字列のn番目(0始まり)の文字の先頭ポインタを返す */
const u8 *utf8_char_at(const u8 *s, int n)
{
    int i;
    for (i = 0; i < n; i++) {
        if (*s == '\0') return NULL;
        s += utf8_char_bytes(s);
    }
    if (*s == '\0') return NULL;
    return s;
}
