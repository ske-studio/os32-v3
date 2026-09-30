/* ======================================================================== */
/*  KBD_STATUS_HOST.C — drivers/kbd_status.c と kernel/v86_kbd.c を         */
/*  そのままホストで回す                                                    */
/*                                                                          */
/*  実物を 1 行も写さずに #include する (-I で解決。変異は写しの上で)。     */
/*  I/O を持たないので模型は要らない。見るのは                              */
/*  (記録: kbd_status_tdd.md、経緯: docs/POLICY_DEBUG.md §4-57):            */
/*    (a) RxRDY = 0 → EMPTY (エラービットが立っていても 0041h は読まない)    */
/*    (b) RxRDY = 1 で PE / FE のどれか 1 つでも → ERROR (OE が一緒でも)     */
/*    (c) RxRDY = 1 で OE だけ → OVERRUN (バイトは正しい、使う)             */
/*    (d) RxRDY = 1 でエラー無し → DATA。他のビット (TxRDY / TxEMP / BRK /  */
/*        DSR) は判定に効かない — NP21/W の keyboard_i43 は `status | 0x85`  */
/*        (DSR / TxEMP / TxRDY を常に立てる) を返すので、そこで DATA に      */
/*        ならないとエミュレータの打鍵が全部落ちる。                         */
/*    (e) V86 へ IRQ1 を反射するのは実データ (DATA / OVERRUN) のときだけ     */
/*        — 空 IRQ・エラーで反射するとゲストに偽の ESC が届いた。            */
/*    (f) 仮想 8251 の 0x41 の空読みは前回のバイト (0x00 = ESC ではない)。   */
/* ======================================================================== */
#include <stdio.h>
#include <string.h>
#include "drivers/kbd_status.c"
#include "kernel/v86_kbd.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* (a) 空 IRQ */
static void case_empty(void)
{
    CHECK(kbd_status_classify(0x00) == KBD_ST_EMPTY);
    CHECK(kbd_status_classify(0x85) == KBD_ST_EMPTY);   /* NP21/W の空 */
    CHECK(kbd_status_classify(0x38) == KBD_ST_EMPTY);   /* エラーだけ */
    CHECK(kbd_status_classify(0xFD) == KBD_ST_EMPTY);   /* bit1 以外全部 */
}

/* (b) 化けたバイト。1 ビットずつと、OE と重なったとき */
static void case_error(void)
{
    CHECK(kbd_status_classify(0x02 | 0x08) == KBD_ST_ERROR);          /* PE */
    CHECK(kbd_status_classify(0x02 | 0x20) == KBD_ST_ERROR);          /* FE */
    CHECK(kbd_status_classify(0x02 | 0x08 | 0x10) == KBD_ST_ERROR);   /* PE+OE */
    CHECK(kbd_status_classify(0x02 | 0x20 | 0x10) == KBD_ST_ERROR);   /* FE+OE */
    CHECK(kbd_status_classify(0xFF) == KBD_ST_ERROR);
}

/* (c) オーバーランだけ: 前のバイトを落としたが、いまのバイトは正しい */
static void case_overrun(void)
{
    CHECK(kbd_status_classify(0x02 | 0x10) == KBD_ST_OVERRUN);
    CHECK(kbd_status_classify(0x85 | 0x02 | 0x10) == KBD_ST_OVERRUN); /* NP21/W の溢れ */
    CHECK(kbd_status_classify(0x02 | 0x10 | 0x40 | 0x01) == KBD_ST_OVERRUN);
}

/* (d) 正常。エラー以外のビットは効かない */
static void case_data(void)
{
    unsigned int other;
    CHECK(kbd_status_classify(0x02) == KBD_ST_DATA);
    CHECK(kbd_status_classify(0x85 | 0x02) == KBD_ST_DATA);   /* NP21/W の打鍵 */
    /* bit0 / bit2 / bit6 / bit7 の全組み合わせ */
    for (other = 0; other < 16; other++) {
        unsigned char st = (unsigned char)(0x02
            | ((other & 1) ? 0x01 : 0) | ((other & 2) ? 0x04 : 0)
            | ((other & 4) ? 0x40 : 0) | ((other & 8) ? 0x80 : 0));
        CHECK(kbd_status_classify(st) == KBD_ST_DATA);
    }
}

/* (e) 反射の判定。0041h から使うバイトを読んだときだけ */
static void case_reflect(void)
{
    unsigned int st;
    CHECK(kbd_status_reflects(KBD_ST_DATA) == 1);
    CHECK(kbd_status_reflects(KBD_ST_OVERRUN) == 1);
    CHECK(kbd_status_reflects(KBD_ST_EMPTY) == 0);
    CHECK(kbd_status_reflects(KBD_ST_ERROR) == 0);
    /* 0043h の 256 通りすべてで: RxRDY かつ PE/FE 無し ⇔ 反射 */
    for (st = 0; st < 256; st++) {
        int want = (st & 0x02) && !(st & (0x08 | 0x20));
        CHECK(kbd_status_reflects(kbd_status_classify((unsigned char)st))
              == want);
    }
}

/* (f) 仮想 8251 の空読み */
static void case_v86empty(void)
{
    v86_kbd_reset();
    /* セッション直後の空読み: ESC のメイク (0x00) を返さない */
    CHECK(v86_kbd_in(V86_KBD_DATA) != 0x00);
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0xFF);
    CHECK((v86_kbd_in(V86_KBD_CMD) & 0x02) == 0);          /* RxRDY = 0 */

    CHECK(v86_kbd_push(0x1E) == 0);
    CHECK((v86_kbd_in(V86_KBD_CMD) & 0x02) != 0);          /* RxRDY = 1 */
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0x1E);
    CHECK((v86_kbd_in(V86_KBD_CMD) & 0x02) == 0);
    /* 空になった後の読みは前回のバイト (実チップと同じ) */
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0x1E);
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0x1E);
    CHECK(v86_kbd_n_read == 1);                            /* 空読みは数えない */

    CHECK(v86_kbd_push(0x9E) == 0);
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0x9E);
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0x9E);

    /* 次のセッションには持ち越さない */
    v86_kbd_reset();
    CHECK(v86_kbd_in(V86_KBD_DATA) == 0xFF);
}

int main(int argc, char **argv)
{
    const char *c = argc > 1 ? argv[1] : "";
    if (!strcmp(c, "empty")) case_empty();
    else if (!strcmp(c, "error")) case_error();
    else if (!strcmp(c, "overrun")) case_overrun();
    else if (!strcmp(c, "data")) case_data();
    else if (!strcmp(c, "reflect")) case_reflect();
    else if (!strcmp(c, "v86empty")) case_v86empty();
    else { fprintf(stderr, "unknown case %s\n", c); return 2; }
    return failed ? 1 : 0;
}
