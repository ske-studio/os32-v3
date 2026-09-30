/* ======================================================================== */
/*  KPRINTF_ATTR_HOST.C — lib/kprintf_attr.c をそのままホストで回す         */
/*                                                                          */
/*  実物の変換を 1 行も写さずに #include する。kprintf_attr_to_pc98 は      */
/*  I/O も VRAM も触らない純粋関数なので、模型は 1 つも要らない。           */
/*                                                                          */
/*  見るのは実機でしか見えない 2 つのこと (記録: kprintf_attr_tdd.md):      */
/*    (a) PC/AT (CGA) 流の属性が PC-98 の「見える色」に化けること           */
/*    (b) 既に PC-98 流で書かれている属性 (0xC1 等) を壊さないこと          */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../lib/kprintf_attr.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

#define CHECK_MAP(in, out) do { \
    u8 got = kprintf_attr_to_pc98((u8)(in)); \
    if (got != (u8)(out)) { \
        fprintf(stderr, "FAIL %s:%d: %02x -> %02x (want %02x)\n", \
                __func__, __LINE__, (unsigned)(in), got, (unsigned)(out)); \
        failed++; \
    } \
} while (0)

static int failed;

/* ------------------------------------------------------------------ */
/*  (a) 実際にソースに書かれている属性の写像                            */
/*                                                                      */
/*  カーネルの kprintf 呼び出しで実在する値。票の期待値そのまま。       */
/* ------------------------------------------------------------------ */
static void real_callers(void)
{
    /* 73 か所が使っている 0x07。PC/AT では「明るい灰色」のつもり。
     * PC-98 の属性として直書きすると「色ビット無し (= 黒) + リバース
     * + ブリンク + 表示」になり、**画面に 1 文字も読めない**。 */
    CHECK_MAP(0x07, 0xE1);
    /* 以下は bit0 (表示) すら立っていないので完全に不可視だった。 */
    CHECK_MAP(0x0A, 0x81);   /* PC/AT: 明るい緑 */
    CHECK_MAP(0x0C, 0x41);   /* PC/AT: 明るい赤 */
    CHECK_MAP(0x0E, 0xC1);   /* PC/AT: 黄色 (赤+緑) */
    CHECK_MAP(0x02, 0x81);   /* PC/AT: 緑 */
    CHECK_MAP(0x04, 0x41);   /* PC/AT: 赤 */
    CHECK_MAP(0x0B, 0xA1);   /* PC/AT: 明るい水色 (青+緑) */
    /* 色を持たない値。PC-98 の黒はシークレット (何も見えない) なので
     * 白へ倒す — 既定色のつもりの kprintf(0, …) を黙って消さない。 */
    CHECK_MAP(0x00, 0xE1);
}

/* ------------------------------------------------------------------ */
/*  (b) 既に PC-98 流の属性はそのまま                                   */
/* ------------------------------------------------------------------ */
static void pc98_passthrough(void)
{
    /* 正しく書かれている呼び出し (kselftest の FAIL 行など)。 */
    CHECK_MAP(0xC1, 0xC1);   /* TATTR_YELLOW */
    CHECK_MAP(0xE1, 0xE1);   /* TATTR_WHITE */
    CHECK_MAP(0xA1, 0xA1);   /* TATTR_CYAN */
    CHECK_MAP(0x81, 0x81);   /* TATTR_GREEN */
    CHECK_MAP(0x41, 0x41);   /* TATTR_RED */
    /* 色は指定されているが bit0 (表示) が落ちている = 見えない。
     * 色を指定したのに消したい、は意図ではあり得ないので立てる。 */
    CHECK_MAP(0xE0, 0xE1);
    CHECK_MAP(0x80, 0x81);
    CHECK_MAP(0x40, 0x41);
    CHECK_MAP(0x20, 0x21);
    /* リバースやアンダーラインを添えた PC-98 流の属性も保つ
     * (色ビットが立っていれば「PC-98 として書かれている」と読む)。 */
    CHECK_MAP(0xE5, 0xE5);   /* 白 + リバース */
    CHECK_MAP(0xE9, 0xE9);   /* 白 + アンダーライン */
    CHECK_MAP(0xE3, 0xE3);   /* 白 + ブリンク */
}

/* ------------------------------------------------------------------ */
/*  (c) 変換が満たすべき性質                                            */
/* ------------------------------------------------------------------ */
static void invariants(void)
{
    unsigned a;

    for (a = 0; a <= 0xFF; a++) {
        u8 out = kprintf_attr_to_pc98((u8)a);

        /* 1. **どの入力でも必ず見える**。bit0 (表示) が立ち、
         *    色ビット (E0h) が 1 つ以上ある。これが崩れた形が、
         *    実機で診断行が 1 行も出なかった状態そのもの。 */
        CHECK((out & TATTR_VISIBLE) != 0);
        CHECK((out & KPRINTF_PC98_COLOR_MASK) != 0);

        /* 2. 冪等。出力をもう一度通しても変わらない
         *    (kprintf の入口で 1 回だけ通す前提が崩れても壊れない)。 */
        CHECK(kprintf_attr_to_pc98(out) == out);

        /* 3. PC/AT 流の入力に反転も点滅も足さない。
         *    07h を「リバース + ブリンク」と読んでしまうのが元の姿。 */
        if ((a & KPRINTF_PC98_COLOR_MASK) == 0) {
            CHECK((out & TATTR_BLINK) == 0);
            CHECK((out & TATTR_REVERSE) == 0);
            CHECK((out & TATTR_UNDERLINE) == 0);
            CHECK((out & TATTR_VLINE) == 0);
        }
    }
}

/* ------------------------------------------------------------------ */
/*  (d) CGA の前景色ビットの対応 (輝度は捨てる)                         */
/* ------------------------------------------------------------------ */
static void cga_bits(void)
{
    unsigned lo;

    /* bit3 (輝度) は PC-98 のテキスト属性に無いので、あっても無くても
     * 同じ色になること。0x00..0x07 と 0x08..0x0F が 1 対 1。 */
    for (lo = 0; lo <= 7; lo++) {
        CHECK(kprintf_attr_to_pc98((u8)lo)
              == kprintf_attr_to_pc98((u8)(lo | 0x08)));
    }

    /* 個々のビットの対応。 */
    CHECK_MAP(0x01, TATTR_B_BLUE | TATTR_VISIBLE);
    CHECK_MAP(0x02, TATTR_B_GREEN | TATTR_VISIBLE);
    CHECK_MAP(0x04, TATTR_B_RED | TATTR_VISIBLE);
    CHECK_MAP(0x03, TATTR_B_BLUE | TATTR_B_GREEN | TATTR_VISIBLE);
    CHECK_MAP(0x05, TATTR_B_BLUE | TATTR_B_RED | TATTR_VISIBLE);
    CHECK_MAP(0x06, TATTR_B_GREEN | TATTR_B_RED | TATTR_VISIBLE);
    CHECK_MAP(0x07, TATTR_WHITE);

    /* PC-98 の上位ビットが 1 つでも立っていたら PC/AT としては読まない
     * — 0x21 (青 + 表示) を「青 + 緑」に化けさせない。 */
    CHECK_MAP(0x21, 0x21);
    CHECK_MAP(0x82, 0x83);   /* 緑 + ブリンク。色があるので素通し + 表示 */
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "real_callers")) real_callers();
    else if (!strcmp(argv[1], "pc98_passthrough")) pc98_passthrough();
    else if (!strcmp(argv[1], "invariants")) invariants();
    else if (!strcmp(argv[1], "cga_bits")) cga_bits();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
