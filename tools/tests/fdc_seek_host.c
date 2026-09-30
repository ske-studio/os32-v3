/* ======================================================================== */
/*  FDC_SEEK_HOST.C — drivers/fdc_decide.c をそのままホストで回す           */
/*                                                                          */
/*  実物の判定を 1 行も写さずに #include する。fdc_decide.c は I/O も        */
/*  tick_count も触らないので、模型は 1 つも要らない。                      */
/*                                                                          */
/*  見るのは実機でしか踏まない 2 つの分岐 (記録: fdc_seek_tdd.md):          */
/*    (a) pending 無しの SIS は ST0 だけの 1 バイト応答                     */
/*    (b) RECALIBRATE の EC は失敗ではなく「もう一度出す」合図              */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/fdc_decide.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* ------------------------------------------------------------------ */
/*  (a) SIS のリザルト長                                               */
/* ------------------------------------------------------------------ */
static void sis_len(void)
{
    /* pending 無し = invalid command 応答。ST0 だけの 1 バイト。
     * ここを 2 と答えると、排水ループが 1 周ごとに来ないバイトを
     * FDC_TIMEOUT_LOOP 回待つ (実機・NP21/W ともこの応答)。 */
    CHECK(fdc_sis_result_bytes(0x80) == FDC_SIS_LEN_INVALID);

    /* 正常終了 (SEEK 完了、ドライブ 0 / ヘッド 0) は ST0 + PCN。 */
    CHECK(fdc_sis_result_bytes(0x20) == FDC_SIS_LEN_NORMAL);
    /* ドライブ 1 / ヘッド 1 の完了も 2 バイト。 */
    CHECK(fdc_sis_result_bytes(0x25) == FDC_SIS_LEN_NORMAL);
    /* 異常終了 (IC=01b) も PCN は続く。 */
    CHECK(fdc_sis_result_bytes(0x60) == FDC_SIS_LEN_NORMAL);
    /* EC 付きの異常終了も 2 バイト。 */
    CHECK(fdc_sis_result_bytes(0x70) == FDC_SIS_LEN_NORMAL);
    /* Ready 線の変化 (IC=11b) も 2 バイト — **80h と紛らわしいが別物**。
     * ここを IC の bit7 だけで見ると C0h まで 1 バイト扱いになり、
     * ディスクを入れ替えたときに PCN が 1 バイト残って以後の
     * リザルトが 1 バイトずつずれる。 */
    CHECK(fdc_sis_result_bytes(0xC0) == FDC_SIS_LEN_NORMAL);
    CHECK(fdc_sis_result_bytes(0xC1) == FDC_SIS_LEN_NORMAL);

    /* invalid は IC の 2 ビットが 10b であること。下位ビットは付かない
     * (invalid 応答にドライブ番号は乗らない) が、念のため 80h だけを
     * 1 バイトと答えることを確かめる。 */
    CHECK(fdc_sis_result_bytes(0x00) == FDC_SIS_LEN_NORMAL);
}

/* ------------------------------------------------------------------ */
/*  (b) シーク完了の判定                                               */
/* ------------------------------------------------------------------ */
static void seek_ok(void)
{
    /* SEEK 正常完了: IC=00b, SE=1, ドライブ 0。PCN は要求シリンダ。 */
    CHECK(fdc_classify_seek_end(0x20, 40, 40) == FDC_SEEK_OK);
    /* ヘッド 1 / ドライブ 1 でも同じ。 */
    CHECK(fdc_classify_seek_end(0x25, 79, 79) == FDC_SEEK_OK);
    /* PCN が違えばシークは外れている。 */
    CHECK(fdc_classify_seek_end(0x20, 39, 40) == FDC_SEEK_FAIL);

    /* RECALIBRATE は want_cyl < 0 で呼ぶ。成功条件は SE=1 かつ EC=0 だけ。
     * **PCN を 0 と決め打って照合しない** — 票の成功条件のとおり。 */
    CHECK(fdc_classify_seek_end(0x20, 0, -1) == FDC_SEEK_OK);
    CHECK(fdc_classify_seek_end(0x20, 77, -1) == FDC_SEEK_OK);
}

static void seek_ec(void)
{
    /* EC = 77 ステップ踏んでもトラック 0 センサが反応しなかった。
     * 80 シリンダ媒体でヘッドが 77 より奥に居ると **正常な機械でも立つ**。
     * 失敗ではなく「もう一度 RECALIBRATE を出す」合図にする。
     * 実際の ST0 は IC=01b + SE + EC = 70h。 */
    CHECK(fdc_classify_seek_end(0x70, 0, -1) == FDC_SEEK_RETRY_EC);
    CHECK(fdc_classify_seek_end(0x71, 0, -1) == FDC_SEEK_RETRY_EC);
    /* SEEK 側で EC が立つことは通常無いが、立ったら同じ扱いにする
     * (PCN の照合より EC が先 — EC のとき PCN は当てにならない)。 */
    CHECK(fdc_classify_seek_end(0x70, 0, 40) == FDC_SEEK_RETRY_EC);
}

static void seek_pending(void)
{
    /* ST0=80h は「読み出す割り込みが無い」= まだ終わっていない。
     * SE も EC も立っていないので、SE だけを見る判定では FAIL に化ける。
     * 呼び出し側はこれを「取りこぼしではなく未完了」と区別する必要がある
     * — タイムアウト後の救済 (SE が立っていれば完了扱い) を、
     *   終わっていないシークにまで広げないため。 */
    CHECK(fdc_classify_seek_end(0x80, 0, 40) == FDC_SEEK_PENDING);
    CHECK(fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING);
}

/* NR は他の失敗と分ける。**リトライも回復もしない**ので、呼び出し側が
 * 即座に最終失敗へ落とせる (HDD 起動時の /fd0 サブマウント試行が空の
 * ドライブで毎回 fdc_recover を 3 回踏んでいたのを止めるため)。 */
static void seek_not_ready(void)
{
    /* SE は立つが媒体が無い (µPD765A / NP21/W とも RECALIBRATE でこの形)。 */
    CHECK(fdc_classify_seek_end(0x28, 40, 40) == FDC_SEEK_NOT_READY);
    CHECK(fdc_classify_seek_end(0x28, 0, -1) == FDC_SEEK_NOT_READY);
    /* IC=01b 付き。NP21/W の FDC_Recalibrate はドライブ未装着で
     * `us | SE | IC0 | NR` = 0x68 を積む。 */
    CHECK(fdc_classify_seek_end(0x68, 0, -1) == FDC_SEEK_NOT_READY);
    CHECK(fdc_classify_seek_end(0x69, 0, -1) == FDC_SEEK_NOT_READY);
    /* NP21/W の FDC_Seek もディスク無しで `us | SE | IC0 | NR`。 */
    CHECK(fdc_classify_seek_end(0x68, 40, 40) == FDC_SEEK_NOT_READY);

    /* **NR は SE の有無より先**。SE が立たない NR も回復では直らない。 */
    CHECK(fdc_classify_seek_end(0x48, 0, -1) == FDC_SEEK_NOT_READY);
    CHECK(fdc_classify_seek_end(0x08, 0, -1) == FDC_SEEK_NOT_READY);
    /* **NR は EC より先**。両方立っていたら再試行しても媒体は現れない。 */
    CHECK(fdc_classify_seek_end(0x78, 0, -1) == FDC_SEEK_NOT_READY);

    /* ただし 80h (pending 無し) は NR より先 — まだ何も起きていない。 */
    CHECK(fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING);
    /* NR が立っていなければ NOT_READY にしない。 */
    CHECK(fdc_classify_seek_end(0x20, 0, -1) == FDC_SEEK_OK);
    CHECK(fdc_classify_seek_end(0x70, 0, -1) == FDC_SEEK_RETRY_EC);
}

static void seek_fail(void)
{
    /* SE が立っていない異常終了。 */
    CHECK(fdc_classify_seek_end(0x40, 0, 40) == FDC_SEEK_FAIL);
    /* 理由の分からない異常終了 (IC=01b, SE=1, EC=0, NR=0)。
     * SE だけを見る判定ではこれが「完了」に化ける — 成功に倒さない。 */
    CHECK(fdc_classify_seek_end(0x60, 0, -1) == FDC_SEEK_FAIL);
    CHECK(fdc_classify_seek_end(0x60, 40, 40) == FDC_SEEK_FAIL);
    /* Ready 線の変化 (IC=11b)。ディスクが抜かれた — やり直しても無駄。 */
    CHECK(fdc_classify_seek_end(0xC0, 0, -1) == FDC_SEEK_FAIL);
    CHECK(fdc_classify_seek_end(0xE0, 0, -1) == FDC_SEEK_FAIL);
    /* 何も立っていない ST0。 */
    CHECK(fdc_classify_seek_end(0x00, 0, -1) == FDC_SEEK_FAIL);
}

/* 実機の事故をそのまま並べた筋書き。
 * ローダ直後 (ヘッドはシリンダ 20〜40) に fdc_init() が RECALIBRATE を
 * 出す → 200ms で諦める → 遅れて来た完了を読まずに次を出す、が元の姿。 */
static void real_hw_story(void)
{
    /* 1. タイムアウト直後に SIS を出したら SE が立っていた
     *    = シークは終わっていて、PIC がエッジを取りこぼしただけ。 */
    CHECK(fdc_classify_seek_end(0x20, 0, -1) == FDC_SEEK_OK);
    /* 2. 同じくタイムアウト直後、ST0=80h なら本当に終わっていない。
     *    ここを 1 と混同して「完了」にすると、動いていないヘッドの上で
     *    READ DATA を出して ND (セクタが見つからない) を踏む。 */
    CHECK(fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING);
    /* 3. 80 シリンダ媒体の奥から戻しきれず EC。もう一度出せば届く。 */
    CHECK(fdc_classify_seek_end(0x70, 0, -1) == FDC_SEEK_RETRY_EC);
    /* 4. 2 回目で完了。 */
    CHECK(fdc_classify_seek_end(0x20, 0, -1) == FDC_SEEK_OK);

    /* 排水ループの停止条件: pending が尽きたら SIS は 1 バイト応答になる。
     * この 2 つが揃って初めて「上限 4 回」が空振り無しで終わる。 */
    CHECK(fdc_sis_result_bytes(0x20) == FDC_SIS_LEN_NORMAL);
    CHECK(fdc_sis_result_bytes(0x80) == FDC_SIS_LEN_INVALID);
    CHECK(fdc_classify_seek_end(0x80, 0, -1) == FDC_SEEK_PENDING);
}

/* 期限内に **別ドライブの通知が割り込む** 筋書き。
 * drv0 のシークを待っているあいだに drv1 の Ready 変化で IRQ が上がると、
 * それを読み捨てた直後の SIS は 80h (pending 無し) を返す。
 * **ここで諦めると drv0 の正常なシークが失敗に化ける** ので、
 * 呼び出し側は 80h を「期限までまだ待つ」と読まなければならない。
 * 判定関数の側で要るのは「3 つが別々の値であること」。 */
static void other_drive(void)
{
    /* 1. drv1 の Ready 変化 (IC=11b)。DS ビットが 1 = 自分宛てではない。 */
    CHECK(fdc_classify_seek_end(0xC1, 0, 40) == FDC_SEEK_FAIL);
    CHECK((0xC1 & FDC_ST0_DS_MASK) == 1);
    /* 2. 読み捨てた直後の SIS は pending 無し = まだ待つ。
     *    FDC_SEEK_FAIL と同じ値だったら、ここで打ち切るしかなくなる。 */
    CHECK(fdc_classify_seek_end(0x80, 0, 40) == FDC_SEEK_PENDING);
    CHECK(FDC_SEEK_PENDING != FDC_SEEK_FAIL);
    /* 3. やがて drv0 の完了が来る。DS ビットは 0 = 自分宛て。 */
    CHECK(fdc_classify_seek_end(0x20, 40, 40) == FDC_SEEK_OK);
    CHECK((0x20 & FDC_ST0_DS_MASK) == 0);

    /* drv1 が未接続で NR を返す場合も、DS ビットで自分宛てでないと分かる。 */
    CHECK(fdc_classify_seek_end(0x69, 0, -1) == FDC_SEEK_NOT_READY);
    CHECK((0x69 & FDC_ST0_DS_MASK) == 1);
    /* NOT_READY は PENDING とも FAIL とも別の値 (即時最終失敗の合図)。 */
    CHECK(FDC_SEEK_NOT_READY != FDC_SEEK_FAIL);
    CHECK(FDC_SEEK_NOT_READY != FDC_SEEK_PENDING);
}

int main(int argc, char **argv)
{
    if (argc != 2) return 2;
    failed = 0;
    if (!strcmp(argv[1], "sis_len")) sis_len();
    else if (!strcmp(argv[1], "seek_ok")) seek_ok();
    else if (!strcmp(argv[1], "seek_ec")) seek_ec();
    else if (!strcmp(argv[1], "seek_pending")) seek_pending();
    else if (!strcmp(argv[1], "seek_fail")) seek_fail();
    else if (!strcmp(argv[1], "seek_not_ready")) seek_not_ready();
    else if (!strcmp(argv[1], "real_hw_story")) real_hw_story();
    else if (!strcmp(argv[1], "other_drive")) other_drive();
    else return 2;
    if (failed) return 1;
    printf("PASS %s\n", argv[1]);
    return 0;
}
