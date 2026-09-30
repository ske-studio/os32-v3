/* ======================================================================== */
/*  IME_ROMKANA.C — ローマ字→かな変換エンジン                                */
/*                                                                          */
/*  ローマ字テーブル照合・促音処理・n待ち処理・ひらがなカタカナ変換           */
/* ======================================================================== */

#include "ime.h"
#include "kstring.h"

/* ======================================================================== */
/*  ローマ字→かな変換テーブル                                                */
/* ======================================================================== */

typedef struct {
    const char *roma;
    const char *kana;
} RomaKana;

static const RomaKana romaji_table[] = {
    {"a", "\xe3\x81\x82"}, {"i", "\xe3\x81\x84"}, {"u", "\xe3\x81\x86"},
    {"e", "\xe3\x81\x88"}, {"o", "\xe3\x81\x8a"},
    {"ka", "\xe3\x81\x8b"}, {"ki", "\xe3\x81\x8d"}, {"ku", "\xe3\x81\x8f"},
    {"ke", "\xe3\x81\x91"}, {"ko", "\xe3\x81\x93"},
    {"sa", "\xe3\x81\x95"}, {"shi", "\xe3\x81\x97"}, {"si", "\xe3\x81\x97"},
    {"su", "\xe3\x81\x99"}, {"se", "\xe3\x81\x9b"}, {"so", "\xe3\x81\x9d"},
    {"ta", "\xe3\x81\x9f"}, {"chi", "\xe3\x81\xa1"}, {"ti", "\xe3\x81\xa1"},
    {"tsu", "\xe3\x81\xa4"}, {"tu", "\xe3\x81\xa4"},
    {"te", "\xe3\x81\xa6"}, {"to", "\xe3\x81\xa8"},
    {"na", "\xe3\x81\xaa"}, {"ni", "\xe3\x81\xab"}, {"nu", "\xe3\x81\xac"},
    {"ne", "\xe3\x81\xad"}, {"no", "\xe3\x81\xae"},
    {"ha", "\xe3\x81\xaf"}, {"hi", "\xe3\x81\xb2"}, {"fu", "\xe3\x81\xb5"},
    {"hu", "\xe3\x81\xb5"}, {"he", "\xe3\x81\xb8"}, {"ho", "\xe3\x81\xbb"},
    {"ma", "\xe3\x81\xbe"}, {"mi", "\xe3\x81\xbf"}, {"mu", "\xe3\x82\x80"},
    {"me", "\xe3\x82\x81"}, {"mo", "\xe3\x82\x82"},
    {"ya", "\xe3\x82\x84"}, {"yu", "\xe3\x82\x86"}, {"yo", "\xe3\x82\x88"},
    {"ra", "\xe3\x82\x89"}, {"ri", "\xe3\x82\x8a"}, {"ru", "\xe3\x82\x8b"},
    {"re", "\xe3\x82\x8c"}, {"ro", "\xe3\x82\x8d"},
    {"wa", "\xe3\x82\x8f"}, {"wo", "\xe3\x82\x92"}, {"nn", "\xe3\x82\x93"},
    {"ga", "\xe3\x81\x8c"}, {"gi", "\xe3\x81\x8e"}, {"gu", "\xe3\x81\x90"},
    {"ge", "\xe3\x81\x92"}, {"go", "\xe3\x81\x94"},
    {"za", "\xe3\x81\x96"}, {"ji", "\xe3\x81\x98"}, {"zi", "\xe3\x81\x98"},
    {"zu", "\xe3\x81\x9a"}, {"ze", "\xe3\x81\x9c"}, {"zo", "\xe3\x81\x9e"},
    {"da", "\xe3\x81\xa0"}, {"di", "\xe3\x81\xa2"}, {"du", "\xe3\x81\xa5"},
    {"de", "\xe3\x81\xa7"}, {"do", "\xe3\x81\xa9"},
    {"ba", "\xe3\x81\xb0"}, {"bi", "\xe3\x81\xb3"}, {"bu", "\xe3\x81\xb6"},
    {"be", "\xe3\x81\xb9"}, {"bo", "\xe3\x81\xbc"},
    {"pa", "\xe3\x81\xb1"}, {"pi", "\xe3\x81\xb4"}, {"pu", "\xe3\x81\xb7"},
    {"pe", "\xe3\x81\xba"}, {"po", "\xe3\x81\xbd"},
    {"kya", "\xe3\x81\x8d\xe3\x82\x83"},
    {"kyu", "\xe3\x81\x8d\xe3\x82\x85"},
    {"kyo", "\xe3\x81\x8d\xe3\x82\x87"},
    {"sha", "\xe3\x81\x97\xe3\x82\x83"},
    {"shu", "\xe3\x81\x97\xe3\x82\x85"},
    {"sho", "\xe3\x81\x97\xe3\x82\x87"},
    {"sya", "\xe3\x81\x97\xe3\x82\x83"},
    {"syu", "\xe3\x81\x97\xe3\x82\x85"},
    {"syo", "\xe3\x81\x97\xe3\x82\x87"},
    {"cha", "\xe3\x81\xa1\xe3\x82\x83"},
    {"chu", "\xe3\x81\xa1\xe3\x82\x85"},
    {"cho", "\xe3\x81\xa1\xe3\x82\x87"},
    {"tya", "\xe3\x81\xa1\xe3\x82\x83"},
    {"tyu", "\xe3\x81\xa1\xe3\x82\x85"},
    {"tyo", "\xe3\x81\xa1\xe3\x82\x87"},
    {"nya", "\xe3\x81\xab\xe3\x82\x83"},
    {"nyu", "\xe3\x81\xab\xe3\x82\x85"},
    {"nyo", "\xe3\x81\xab\xe3\x82\x87"},
    {"hya", "\xe3\x81\xb2\xe3\x82\x83"},
    {"hyu", "\xe3\x81\xb2\xe3\x82\x85"},
    {"hyo", "\xe3\x81\xb2\xe3\x82\x87"},
    {"mya", "\xe3\x81\xbf\xe3\x82\x83"},
    {"myu", "\xe3\x81\xbf\xe3\x82\x85"},
    {"myo", "\xe3\x81\xbf\xe3\x82\x87"},
    {"rya", "\xe3\x82\x8a\xe3\x82\x83"},
    {"ryu", "\xe3\x82\x8a\xe3\x82\x85"},
    {"ryo", "\xe3\x82\x8a\xe3\x82\x87"},
    {"gya", "\xe3\x81\x8e\xe3\x82\x83"},
    {"gyu", "\xe3\x81\x8e\xe3\x82\x85"},
    {"gyo", "\xe3\x81\x8e\xe3\x82\x87"},
    {"ja", "\xe3\x81\x98\xe3\x82\x83"},
    {"ju", "\xe3\x81\x98\xe3\x82\x85"},
    {"jo", "\xe3\x81\x98\xe3\x82\x87"},
    {"bya", "\xe3\x81\xb3\xe3\x82\x83"},
    {"byu", "\xe3\x81\xb3\xe3\x82\x85"},
    {"byo", "\xe3\x81\xb3\xe3\x82\x87"},
    {"pya", "\xe3\x81\xb4\xe3\x82\x83"},
    {"pyu", "\xe3\x81\xb4\xe3\x82\x85"},
    {"pyo", "\xe3\x81\xb4\xe3\x82\x87"},
    {"xtu", "\xe3\x81\xa3"}, {"xtsu", "\xe3\x81\xa3"},
    {"xa", "\xe3\x81\x81"}, {"xi", "\xe3\x81\x83"},
    {"xu", "\xe3\x81\x85"}, {"xe", "\xe3\x81\x87"}, {"xo", "\xe3\x81\x89"},
    {"xya", "\xe3\x82\x83"}, {"xyu", "\xe3\x82\x85"}, {"xyo", "\xe3\x82\x87"},
    {"", ""}
};

/* テーブルに対する前方一致チェック */
static int rk_has_prefix(const char *temp, int temp_len)
{
    int i, j, match;
    for (i = 0; romaji_table[i].roma[0] != '\0'; i++) {
        if ((int)kstrlen(romaji_table[i].roma) > temp_len) {
            match = 1;
            for (j = 0; j < temp_len; j++) {
                if (romaji_table[i].roma[j] != temp[j]) { match = 0; break; }
            }
            if (match) return 1;
        }
    }
    return 0;
}

void ime_rk_init(IME_RomKana *rk)
{
    rk->preedit[0] = '\0';
    rk->output[0] = '\0';
    rk->n_wait = 0;
}

int ime_rk_append(IME_RomKana *rk, char c)
{
    int i, len;
    char temp[8];

    rk->output[0] = '\0';

    /* 記号変換 */
    if (c < 'a' || c > 'z') {
        if (c == '-') { kstrcpy(rk->output, "\xe3\x83\xbc"); }
        else if (c == ',') { kstrcpy(rk->output, "\xe3\x80\x81"); }
        else if (c == '.') { kstrcpy(rk->output, "\xe3\x80\x82"); }
        else if (c == '[') { kstrcpy(rk->output, "\xe3\x80\x8c"); }
        else if (c == ']') { kstrcpy(rk->output, "\xe3\x80\x8d"); }
        else {
            rk->output[0] = c;
            rk->output[1] = '\0';
        }
        rk->preedit[0] = '\0';
        rk->n_wait = 0;
        return 1;
    }

    /* n 待ち状態の処理 */
    if (rk->n_wait) {
        if (c == 'n') {
            kstrcpy(rk->output, "\xe3\x82\x93"); /* ん */
            rk->preedit[0] = '\0';
            rk->n_wait = 0;
            return 1;
        } else if (c == 'y' || c == 'a' || c == 'i' || c == 'u' ||
                   c == 'e' || c == 'o') {
            rk->n_wait = 0;
        } else {
            kstrcpy(rk->output, "\xe3\x82\x93"); /* ん */
            rk->preedit[0] = c;
            rk->preedit[1] = '\0';
            rk->n_wait = 0;
            return 1;
        }
    }

    /* n 単独 */
    if (rk->preedit[0] == '\0' && c == 'n') {
        rk->preedit[0] = 'n';
        rk->preedit[1] = '\0';
        rk->n_wait = 1;
        return 0;
    }

    len = (int)kstrlen(rk->preedit);

    /* preedit も temp も 8 バイト。len は最大 7 まであり得るので、
     * 無条件に temp[len]=c / temp[len+1]='\0' と書くと temp[8] が
     * スタックを 1 バイト踏む。ここまで伸びた時点でテーブルには
     * 一致しない (最長のローマ字綴りでも 4 文字) ので、打ち切って
     * 入力し直しにする。 */
    if (len + 2 > (int)sizeof(temp)) {
        rk->preedit[0] = c;
        rk->preedit[1] = '\0';
        rk->n_wait = 0;
        return 0;
    }

    /* 促音: 同一子音連続 (tt, kk, ss 等) */
    if (len == 1 && rk->preedit[0] == c &&
        c != 'n' && c != 'a' && c != 'i' && c != 'u' &&
        c != 'e' && c != 'o') {
        kstrcpy(rk->output, "\xe3\x81\xa3"); /* っ */
        rk->preedit[0] = c;
        rk->preedit[1] = '\0';
        return 1;
    }

    /* 結合してテーブル照合 */
    kstrcpy(temp, rk->preedit);
    temp[len] = c;
    temp[len + 1] = '\0';

    for (i = 0; romaji_table[i].roma[0] != '\0'; i++) {
        if (kstrcmp(romaji_table[i].roma, temp) == 0) {
            kstrcpy(rk->output, romaji_table[i].kana);
            rk->preedit[0] = '\0';
            rk->n_wait = 0;
            return 1;
        }
    }

    /* 前方一致あり → まだ続きがある */
    if (rk_has_prefix(temp, (int)kstrlen(temp))) {
        kstrcpy(rk->preedit, temp);
        return 0;
    }

    /* マッチなし → 先頭に c を残して再開 */
    rk->preedit[0] = c;
    rk->preedit[1] = '\0';
    rk->n_wait = 0;
    return 0;
}

int ime_rk_flush_n(IME_RomKana *rk)
{
    if (rk->n_wait) {
        kstrcpy(rk->output, "\xe3\x82\x93"); /* ん */
        rk->preedit[0] = '\0';
        rk->n_wait = 0;
        return 1;
    }
    return 0;
}

/* ======================================================================== */
/*  ひらがな→カタカナ変換                                                    */
/* ======================================================================== */

void ime_hira_to_kata(char *utf8_str)
{
    u8 *p = (u8 *)utf8_str;
    u16 code;
    while (*p) {
        if ((p[0] & 0xF0) == 0xE0 && (p[1] & 0xC0) == 0x80 &&
            (p[2] & 0xC0) == 0x80) {
            code = ((p[0] & 0x0F) << 12) | ((p[1] & 0x3F) << 6) |
                   (p[2] & 0x3F);
            if (code >= 0x3041 && code <= 0x3096) {
                code += 0x0060;
                p[0] = 0xE0 | ((code >> 12) & 0x0F);
                p[1] = 0x80 | ((code >>  6) & 0x3F);
                p[2] = 0x80 | (code & 0x3F);
            }
            p += 3;
        } else if ((p[0] & 0xE0) == 0xC0) {
            p += 2;
        } else {
            p += 1;
        }
    }
}
