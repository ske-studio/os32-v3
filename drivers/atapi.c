/* ======================================================================== */
/*  ATAPI.C — PC-98 IDE/ATAPI CD-ROM PIOドライバ実装                        */
/*                                                                          */
/*  IDEセカンダリバンクに接続されたATAPI CD-ROMデバイスに対して、PACKETコマ  */
/*  ンド (0xA0) を発行し、SCSIコマンド (READ(10)等) でデータ転送を行う。    */
/*                                                                          */
/*  プロトコル (PIOモード):                                                  */
/*    1. バンク切替 → セカンダリIDE選択                                     */
/*    2. コマンドレジスタに 0xA0 書込み                                     */
/*    3. DRQ + CD 待ち → 12バイトCDBを6ワード送出                          */
/*    4. データ転送: CylHi:Lo = バイト数 → データレジスタから読み出し      */
/*                                                                          */
/*  参照: NP21/W (atapicmd.c L206-428, ideio.c L932-948)                   */
/* ======================================================================== */

#include "atapi.h"
#include "io.h"
#include "pc98.h"
#include "kprintf.h"
#include "cpu_calibrate.h"   /* cpu_delay_us / CPU_DELAY_US_MAX (INC_KERNEL で通す。build/kernel.mk) */

/* === 内部状態 === */
static int cdrom_present = 0;
/* 使う装置の選択 (DRV_HEAD に書く値)。セカンダリのマスター = 0x00、
 * スレーブ = ATAPI_DRV_SLAVE。atapi_init が媒体の入っている方を選ぶ */
static u8 s_drvsel = 0x00;
/* セカンダリのバスが覚えている選択 (最後に DRV_HEAD へ書いた値)。バンクを
 * 切り替えてもバスの側は変わらない。起動直後は ATAPI_SEL_UNKNOWN */
static u8 s_cursel = ATAPI_SEL_UNKNOWN;
/* シグネチャの出た装置 (bit0 = マスター、bit1 = スレーブ)。居ない装置が
 * BSY らしい値を返しても、それを「コマンド未完了」とは見ない */
static u8 s_present_mask = 0;

/* 最後にエラーで終わったコマンドのセンスキー (エラーレジスタの bit7-4)。
 * 次のコマンドを出す前に読まないと消える */
static u8 s_last_sense = 0;
/* 媒体の世代 (atapi_media_gen)。UNIT ATTENTION / NOT READY で進む */
static u32 s_media_gen = 0;
static AtapiStats s_stats;

/* 最後の PACKET の終わり方 (診断の行のため)。st = 最後に読んだステータス
 * (待ちの期限切れでもその時点の値)、err = エラーレジスタ (ERR のときだけ)、
 * got = 受け取った有効バイト数、lba / n = 落ちた READ(10) の引数 */
static u8  s_diag_st = 0;
static u8  s_diag_err = 0;
static u32 s_diag_got = 0;
static u32 s_diag_lba = 0;
static u32 s_diag_n = 0;
/* 読みの失敗の行を出した数 (ATAPI_DIAG_MAX で止める) */
static u32 s_diag_lines = 0;
/* リセット・準備中の行を出した数 (これも ATAPI_DIAG_MAX で止める) */
static u32 s_note_lines = 0;

/* BSY / DRQ の待ちの上限 (µs)。ふだんは ATAPI_CMD_TIMEOUT_US、atapi_init の
 * 間だけ ATAPI_INIT_TIMEOUT_US (atapi.h「待ちの上限」) */
static u32 s_wait_limit_us = ATAPI_CMD_TIMEOUT_US;

/* バスが死んだ印: SRST の後もマスターが ATAPI_SRST_TIMEOUT_US (31 秒) BSY の
 * ままだった。立っているあいだ、公開の読み・容量確認・TEST UNIT READY は
 * バスに触らず ATAPI_ERR_TIMEOUT を返す (触れば毎回 DEVICE RESET と SRST の
 * 待ちで約 61 秒かかる)。解くのは atapi_init だけ (再試行は atapi_init を
 * 呼び直す)。s_dead_noted = 「即失敗」の行を今回の印で出したか */
static int s_bus_dead = 0;
static int s_dead_noted = 0;

/* ======================================================================== */
/*  内部ヘルパー                                                             */
/* ======================================================================== */

/* バンク選択 (0=プライマリ/HDD, 1=セカンダリ/CD-ROM)。装置 (マスター /
 * スレーブ) の選択はここではせず、コマンドを出す直前に atapi_select_device が
 * 行う — バスが BSY の装置を選んでいるかもしれないときに DRV_HEAD を書かない */
static void atapi_select_bank(int bank)
{
    outp(IDE_BANK1, (unsigned)(bank ? 0x01 : 0x00));
}

/* ALT_STATUS を 1 回読む。診断の行のために最後の値を覚える */
static u8 atapi_status(void)
{
    u8 st = (u8)inp(IDE_ALT_STATUS);
    s_diag_st = st;
    return st;
}

/* ALT_STATUS の mask のビットが全部落ちるまで、最大 limit_us 待つ。
 * 読むごとに cpu_delay_us(ATAPI_POLL_US) を挟み、挟んだ時間の合計で数える
 * (時間の上限。回数ではない — atapi.h「待ちの上限」)。すぐ落ちていれば待たない */
static int atapi_wait_clear(u8 mask, u32 limit_us)
{
    u32 waited = 0;
    for (;;) {
        if (!(atapi_status() & mask)) return ATAPI_OK;
        if (waited >= limit_us) return ATAPI_ERR_TIMEOUT;
        cpu_delay_us(ATAPI_POLL_US);
        waited += ATAPI_POLL_US;
    }
}

/* BSY=0 待ち */
static int atapi_wait_bsy(void)
{
    return atapi_wait_clear(IDE_ST_BSY, s_wait_limit_us);
}

/* BSY=0 かつ DRQ=0 待ち (コマンドを出せる状態) */
static int atapi_wait_idle(void)
{
    return atapi_wait_clear(IDE_ST_BSY | IDE_ST_DRQ, s_wait_limit_us);
}

/* DRQ待ち (BSY=0 && DRQ=1)。BSY=0 で ERR なら ATAPI_ERR_IO。上限は合わせて
 * s_wait_limit_us (数え方は atapi_wait_clear と同じ) */
static int atapi_wait_drq(void)
{
    u32 waited = 0;
    for (;;) {
        u8 st = atapi_status();
        if (!(st & IDE_ST_BSY)) {
            if (st & IDE_ST_ERR) return ATAPI_ERR_IO;
            if (st & IDE_ST_DRQ) return ATAPI_OK;
        }
        if (waited >= s_wait_limit_us) return ATAPI_ERR_TIMEOUT;
        cpu_delay_us(ATAPI_POLL_US);
        waited += ATAPI_POLL_US;
    }
}

/* ERR を見たときに呼ぶ。センスキーを覚え、媒体が替わった/替わりつつある
 *印なら世代を進める。 */
static void atapi_note_error(void)
{
    s_diag_err = (u8)inp(IDE_ERROR);
    s_last_sense = (u8)(s_diag_err >> ATAPI_ERR_SENSE_SHIFT);
    if (s_last_sense == ATAPI_SK_UNIT_ATTENTION
        || s_last_sense == ATAPI_SK_NOT_READY) {
        s_media_gen++;
        s_stats.unit_attention++;
    }
}

/* DRQ のブロックを読み終えた直後、デバイスが DRQ を落として BSY を上げる
 * までの 400ns を置く (ここを置かないと、前のブロックの DRQ=1 を読んで
 * もう 1 ブロックあると取り違える)。装置を選び直した直後も同じ。
 * ALT_STATUS の空読みで待つ */
static void atapi_settle(void)
{
    int i;
    for (i = 0; i < IDE_SEL_SETTLE; i++) (void)inp(IDE_ALT_STATUS);
}

/* 12バイトCDBを0クリア */
static void atapi_clear_cdb(u8 *cdb)
{
    int i;
    for (i = 0; i < 12; i++) cdb[i] = 0;
}

/* us だけ待つ。cpu_delay_us は 1 回で CPU_DELAY_US_MAX (100ms) までしか待たず、
 * それより長い指定を黙って丸めるので、CPU_DELAY_US_MAX 以下の塊に分けて呼ぶ。
 * atapi_init は cpu_calibrate の後に走る。tick_count で待たないのは、PIT の
 * 割り込みが来ている (IF=1) ことを前提にできないから — cpu_delay_us は割り込み禁止区間でも待てる */
static void atapi_delay_us(u32 us)
{
    while (us > 0) {
        u32 d = (us > CPU_DELAY_US_MAX) ? CPU_DELAY_US_MAX : us;
        cpu_delay_us(d);
        us -= d;
    }
}

/* sel (DRV_HEAD の値) の装置はシグネチャが出た装置か */
static int atapi_sel_present(u8 sel)
{
    if (sel == 0x00) return (s_present_mask & 0x01) ? 1 : 0;
    if (sel == ATAPI_DRV_SLAVE) return (s_present_mask & 0x02) ? 1 : 0;
    return 0;
}

/* リセット・準備中の 1 行 (ATAPI_DIAG_MAX 行まで)。実機で DEVICE RESET / SRST /
 * START UNIT を踏んだかを画面で分けるため。with_asc なら ASC / ASCQ も出し、
 * そうでなければ limit= にその待ちの上限 (limit_us、秒) を出す */
static void atapi_note_ex(const char *what, int with_asc, u8 asc, u8 ascq,
                          u32 limit_us)
{
    if (s_note_lines >= ATAPI_DIAG_MAX) return;
    s_note_lines++;
    if (with_asc) {
        kprintf(0x07, "[atapi] %s drv=%d st=%02x asc/ascq=%02x/%02x\n", what,
                (s_cursel == ATAPI_DRV_SLAVE) ? 1 : 0, (unsigned)s_diag_st,
                (unsigned)asc, (unsigned)ascq);
    } else {
        kprintf(0x07, "[atapi] %s drv=%d st=%02x limit=%us\n", what,
                (s_cursel == ATAPI_DRV_SLAVE) ? 1 : 0, (unsigned)s_diag_st,
                (unsigned)(limit_us / 1000000UL));
    }
}

/* 上限がふだんの待ち (s_wait_limit_us) の行 */
static void atapi_note(const char *what, int with_asc, u8 asc, u8 ascq)
{
    atapi_note_ex(what, with_asc, asc, ascq, s_wait_limit_us);
}

/* バスが死んでいれば 1 を返す (公開の入口の最初で見る)。印を立ててから最初の
 * 1 回だけ「待たずに断る」行を出し、断った数を s_stats.dead_fails に数える */
static int atapi_bus_dead_fail(void)
{
    if (!s_bus_dead) return 0;
    s_stats.dead_fails++;
    if (!s_dead_noted) {
        s_dead_noted = 1;
        atapi_note_ex("bus dead since SRST timeout, failing at once until atapi_init",
                      0, 0, 0, ATAPI_SRST_TIMEOUT_US);
    }
    return 1;
}

/* SRST: セカンダリのバスの 2 台ともリセットする (バンクで選んだバスだけ。
 * プライマリの HDD には届かない — UNDOCUMENTED io_ide 074Ch、NP21/W ideio_o74c)。
 * 同じバンク (セカンダリ) に ATA の HDD (drivers/ide.c の drive 2 / 3) が
 * つながっていれば、それも戻す。
 * リセット後はマスターが選ばれる。順序は ATA の規定どおり:
 *   SRST を立てる → 5µs 以上 → 解く → 2ms 以上置く → マスターの BSY=0 を待つ。
 * 使う装置の選び直し (DRV_HEAD → 400ns → BSY=0) は呼び手が行う。
 * マスターが居ない (シグネチャが出なかった、または浮いたバス) なら待たない。
 * マスターの BSY は ATA の規定の最大 ATAPI_SRST_TIMEOUT_US (31 秒) まで待つ。
 * スレーブの SRST 後の BSY はここでは待たない — 選び直した後の待ち
 * (s_wait_limit_us) までしか待たない。
 * 戻り値: ATAPI_OK / ATAPI_ERR_TIMEOUT (マスターが BSY のまま — 呼び手は
 * DRV_HEAD を書かない。BSY の装置が選ばれているあいだは書いても届かない)。
 * 期限切れならバスが死んだ印 (s_bus_dead) を立てる */
static int atapi_srst(void)
{
    int i;
    outp(IDE_DEV_CTRL, IDE_NIEN | IDE_SRST);
    for (i = 0; i < ATAPI_SRST_HOLD_LOOP; i++) (void)inp(IDE_ALT_STATUS);
    outp(IDE_DEV_CTRL, IDE_NIEN);
    s_cursel = 0x00;
    s_stats.soft_resets++;
    atapi_delay_us(ATAPI_SRST_SETTLE_US);
    if (s_present_mask != 0 && !atapi_sel_present(0x00)) return ATAPI_OK;
    if (atapi_status() == ATAPI_ST_FLOAT) return ATAPI_OK;
    if (atapi_wait_clear(IDE_ST_BSY, ATAPI_SRST_TIMEOUT_US) != ATAPI_OK) {
        s_bus_dead = 1;
        s_dead_noted = 0;
        atapi_note_ex("SRST: BSY did not clear, bus marked dead", 0, 0, 0,
                      ATAPI_SRST_TIMEOUT_US);
        return ATAPI_ERR_TIMEOUT;
    }
    return ATAPI_OK;
}

/* 選ばれている装置が「固まった」: atapi_select_device が s_wait_limit_us 待っても
 * BSY / DRQ のまま (「遅いだけ」と「固まった」の境目は秒 — atapi.h の
 * ATAPI_CMD_TIMEOUT_US。コマンド自身の待ちが期限切れになってもここへは来ない)。
 * まず DEVICE RESET (08h) — PACKET 装置が BSY でも受ける唯一のコマンドで、
 * その装置だけを戻す。それでも動かなければ SRST でバスの 2 台とも戻し、
 * SRST はマスターを選び直すので使う装置を選び直す。SRST の後もマスターが
 * BSY のままなら DRV_HEAD を書かずに ATAPI_ERR_TIMEOUT。
 * どちらの後も装置は UNIT ATTENTION を立てるので、次のコマンドの出し直しは
 * 呼び手 (atapi_read10 / atapi_capacity_ready) が行う */
static int atapi_recover(void)
{
    atapi_note("DEVICE RESET (BSY/DRQ past the limit twice)", 0, 0, 0);
    outp(IDE_COMMAND, ATAPI_CMD_DEVICE_RESET);
    s_stats.dev_resets++;
    atapi_settle();
    if (atapi_wait_idle() == ATAPI_OK) return ATAPI_OK;
    if (atapi_srst() != ATAPI_OK) return ATAPI_ERR_TIMEOUT;
    outp(IDE_DRV_HEAD, s_drvsel);
    s_cursel = s_drvsel;
    atapi_settle();
    return atapi_wait_idle();
}

/* 使う装置 (s_drvsel) を選ぶ。順序は ATA の規定どおり:
 *   1. バスがいま選んでいる装置が BSY / DRQ なら、それが「居る装置のコマンド
 *      未完了」のときだけ終わるのを待つ (期限切れならリセットで回復)。
 *      浮いたバス (0xFF) や、シグネチャの出なかった装置の値は待たない
 *   2. DRV_HEAD に書く → 400ns 置く
 *   3. 選んだ装置の BSY / DRQ が落ちるのを待つ (落ちなければ回復して、
 *      それでも落ちなければ期限切れ)
 * この後で Features / Byte Count を書き、PACKET を出す */
static int atapi_select_device(void)
{
    u8 st = atapi_status();

    if (st != ATAPI_ST_FLOAT && (st & (IDE_ST_BSY | IDE_ST_DRQ))
        && atapi_sel_present(s_cursel)) {
        if (atapi_wait_idle() != ATAPI_OK
            && atapi_recover() != ATAPI_OK) return ATAPI_ERR_TIMEOUT;
    }
    outp(IDE_DRV_HEAD, s_drvsel);
    s_cursel = s_drvsel;
    atapi_settle();
    if (atapi_wait_idle() != ATAPI_OK) {
        if (atapi_recover() != ATAPI_OK) return ATAPI_ERR_TIMEOUT;
        if (atapi_wait_idle() != ATAPI_OK) return ATAPI_ERR_TIMEOUT;
    }
    return ATAPI_OK;
}

/* ======================================================================== */
/*  PACKETコマンドプロトコル                                                  */
/*                                                                          */
/*  NP21/W ideio.c L932-948:                                                */
/*    case 0xa0: send packet                                                */
/*      → DRQセット + Interrupt Reason = CD|~IO                            */
/*      → ホストが12バイトCDB書込み → atapicmd_a0() 呼び出し              */
/* ======================================================================== */

/* PACKET の共通路: 装置選択 → Features=0 (PIO) / Byte Count = bcl →
 * PACKET → CDB 受付の DRQ 待ち → 12 バイト CDB を 6 ワード送出。
 * 装置選択と待ちを済ませてからレジスタを書く (旧装置に書かない) */
static int atapi_send_cdb(const u8 *cdb, u32 bcl)
{
    int ret;
    int i;

    ret = atapi_select_device();
    if (ret != ATAPI_OK) return ret;

    outp(IDE_FEATURES, 0x00);
    outp(IDE_CYL_LO, (unsigned)(bcl & 0xFF));
    outp(IDE_CYL_HI, (unsigned)((bcl >> 8) & 0xFF));
    outp(IDE_COMMAND, ATAPI_CMD_PACKET);

    ret = atapi_wait_drq();
    if (ret == ATAPI_ERR_IO) atapi_note_error();
    if (ret != ATAPI_OK) return ret;

    for (i = 0; i < 6; i++) {
        u16 w = (u16)cdb[i * 2] | ((u16)cdb[i * 2 + 1] << 8);
        outpw(IDE_DATA, (unsigned)w);
    }
    atapi_settle();
    return ATAPI_OK;
}

/* PACKETコマンド発行: CDB送出 → 完了ステータス待ち (データなし)
 * 戻り値: ATAPI_OK=成功 */
static int atapi_packet_nodata(const u8 *cdb)
{
    int ret;

    ret = atapi_send_cdb(cdb, 0);
    if (ret != ATAPI_OK) return ret;

    /* コマンド完了待ち (BSY=0) */
    ret = atapi_wait_bsy();
    if (ret != ATAPI_OK) return ret;

    /* エラーチェック */
    {
        u8 st = (u8)inp(IDE_STATUS);
        s_diag_st = st;
        if (st & IDE_ST_ERR) {
            atapi_note_error();
            return ATAPI_ERR_IO;
        }
    }

    return ATAPI_OK;
}

/* PACKETコマンド発行: CDB送出 → データ読み出し
 *   cdb:      12バイトCDB
 *   buf:      データ受信バッファ
 *   buf_size: バッファサイズ
 *   actual:   デバイスが渡した有効バイト数 (NULLなら無視)。buf_size を超えた分は
 *             捨てるが、数には入れる (呼び手が「過不足なし」を確かめるため)
 * 戻り値: ATAPI_OK=成功
 *
 * データは 1 回以上の DRQ で来る。各 DRQ のバイト数は Cylinder Low/High に
 * 出る (byte count limit 以下、デバイスが決める)。DRQ が落ちるまで繰り返す。
 * ポートはワード単位なので奇数バイトの DRQ は 1 ワード多く読むが、有効な
 * バイト数はバイト数のまま数える (7 バイトの応答を 8 と数えない)。
 * 途中で落ちても s_diag_st / s_diag_got はその時点の値 */
static int atapi_packet_read(const u8 *cdb, void *buf, u32 buf_size,
                             u32 *actual)
{
    int ret;
    u32 total_read = 0;
    u32 bcl;
    u8 *p = (u8 *)buf;

    if (actual) *actual = 0;
    s_diag_st = 0;
    s_diag_err = 0;
    s_diag_got = 0;

    /* byte count limit: 1 回の DRQ の上限。偶数で、ATAPI_PIO_BCL_MAX 以下 */
    bcl = (buf_size < ATAPI_PIO_BCL_MAX) ? buf_size : ATAPI_PIO_BCL_MAX;
    bcl &= ~1UL;
    if (bcl == 0) bcl = 2;

    ret = atapi_send_cdb(cdb, bcl);
    if (ret != ATAPI_OK) return ret;

    /* データ転送ループ (DRQ ごとに 1 回) */
    while (1) {
        u16 xfer_size;
        u32 words;
        u32 end;
        u32 k;
        u8 st;

        /* BSY=0になるまで待つ */
        ret = atapi_wait_bsy();
        if (ret != ATAPI_OK) {
            s_diag_got = total_read;
            return ret;
        }

        /* ステータス確認 */
        st = (u8)inp(IDE_STATUS);
        s_diag_st = st;
        if (st & IDE_ST_ERR) {
            s_diag_got = total_read;
            atapi_note_error();
            return ATAPI_ERR_IO;
        }

        /* DRQが立っていなければ転送完了 */
        if (!(st & IDE_ST_DRQ)) break;

        /* CylHi:CylLo から転送バイト数を取得 */
        xfer_size = (u16)inp(IDE_CYL_LO) | ((u16)inp(IDE_CYL_HI) << 8);
        if (xfer_size == 0) break;

        /* ワード単位で読み出す (奇数なら 1 ワード多い)。有効なのは xfer_size
         * バイトまで。buf_size を超えた分は読み捨てる */
        words = ((u32)xfer_size + 1) / 2;
        end = total_read + xfer_size;
        for (k = 0; k < words; k++) {
            u16 w = (u16)inpw(IDE_DATA);
            u32 b = total_read + k * 2;
            if (b < end && b < buf_size) p[b] = (u8)(w & 0xFF);
            if (b + 1 < end && b + 1 < buf_size) p[b + 1] = (u8)(w >> 8);
        }
        total_read = end;
        atapi_settle();
    }

    /* IRQクリア */
    { u8 st = (u8)inp(IDE_STATUS); (void)st; }

    s_diag_got = total_read;
    if (actual) *actual = total_read;
    return ATAPI_OK;
}

/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

/* sel (マスター / スレーブ) に ATAPI のシグネチャが出ているか。コマンドを
 * 出す前 (リセットの直後) にだけ意味がある — PACKET の後は CylLo/Hi が
 * byte count に変わる。居ない装置は 0xFF が返るので BSY 待ちをしない */
static int atapi_probe_sig(u8 sel)
{
    u8 cl, ch;

    outp(IDE_DRV_HEAD, sel);
    s_cursel = sel;
    atapi_settle();
    if (atapi_status() == ATAPI_ST_FLOAT) return 0;
    if (atapi_wait_bsy() != ATAPI_OK) return 0;   /* 上限は ATAPI_INIT_TIMEOUT_US */

    /* NP21/W ideio.c: ATAPI デバイスはリセット後 CylLo=0x14, CylHi=0xEB */
    cl = (u8)inp(IDE_CYL_LO);
    ch = (u8)inp(IDE_CYL_HI);
    return (cl == ATAPI_SIG_CYL_LO && ch == ATAPI_SIG_CYL_HI) ? 1 : 0;
}

/* READ CAPACITY (バンクは呼び手が選んでおく)。8 バイトちょうど来なければ失敗 */
static int atapi_capacity_raw(AtapiCapacity *cap)
{
    u8 cdb[12];
    u8 buf[8];
    u32 got = 0;
    int ret;

    atapi_clear_cdb(cdb);
    cdb[0] = SCSI_CMD_READ_CAPACITY;
    ret = atapi_packet_read(cdb, buf, 8, &got);
    /* 受け皿の残りを容量として読まない (7 バイトも 9 バイトも失敗) */
    if (ret == ATAPI_OK && got != 8) ret = ATAPI_ERR_IO;
    if (ret != ATAPI_OK) return ret;

    /* READ CAPACITY応答: ビッグエンディアン
     * bytes 0-3: 最終LBA (NP21/W は総数を返す。空のドライブは 0)
     * bytes 4-7: セクタサイズ */
    cap->total_sectors = ((u32)buf[0] << 24) | ((u32)buf[1] << 16)
                       | ((u32)buf[2] << 8)  |  (u32)buf[3];
    cap->total_sectors += 1;  /* 最終LBA → 総セクタ数 */

    cap->sector_size   = ((u32)buf[4] << 24) | ((u32)buf[5] << 16)
                       | ((u32)buf[6] << 8)  |  (u32)buf[7];
    return ATAPI_OK;
}

/* REQUEST SENSE: センスキーと ASC / ASCQ を読む。UNIT ATTENTION はこれで消える。
 * 読めなければ ATAPI_ERR_IO (sk / asc / ascq は触らない) */
static int atapi_request_sense(u8 *sk, u8 *asc, u8 *ascq)
{
    u8 cdb[12];
    u8 buf[ATAPI_SENSE_LEN];
    u32 got = 0;
    int ret;

    atapi_clear_cdb(cdb);
    cdb[0] = SCSI_CMD_REQUEST_SENSE;
    cdb[4] = ATAPI_SENSE_LEN;
    ret = atapi_packet_read(cdb, buf, ATAPI_SENSE_LEN, &got);
    if (ret != ATAPI_OK) return ret;
    if (got < ATAPI_SENSE_MIN) return ATAPI_ERR_IO;
    *sk   = (u8)(buf[2] & 0x0F);
    *asc  = buf[12];
    *ascq = buf[13];
    return ATAPI_OK;
}

/* START STOP UNIT (開始)。IMMED = 0 なので回り始めるまで BSY — 待ちは PACKET の
 * 上限 (ATAPI_CMD_TIMEOUT_US) */
static int atapi_start_unit(void)
{
    u8 cdb[12];
    atapi_clear_cdb(cdb);
    cdb[0] = SCSI_CMD_START_STOP_UNIT;
    cdb[4] = SCSI_SSU_START;
    s_stats.start_units++;
    return atapi_packet_nodata(cdb);
}

/* READ CAPACITY を、装置が準備中のあいだ出し直す (ATAPI_READY_RETRIES まで):
 *   UNIT ATTENTION (6): 電源投入・リセット・媒体交換の後の最初のコマンドは
 *       仕様どおりこれで落ちる。REQUEST SENSE で消して出し直す
 *   NOT READY (2): ASC 3Ah (媒体なし) だけを ATAPI_ERR_NO_MEDIA と確定する。
 *       04h/02h (initializing command required) は待っても変わらないので
 *       START STOP UNIT (開始) を 1 回だけ出して、待たずに出し直す (票 §2)。
 *       それ以外 (04h/01h 準備中など) は ATAPI_READY_WAIT_US 待って出し直す
 *   他のエラー・期限切れ・長さ違い: そのまま返す
 * 回数が尽きたら、最後が NOT READY なら ATAPI_ERR_NO_MEDIA (最後の ASC / ASCQ を
 * 1 行出す)、他は最後の結果 */
static int atapi_capacity_ready(AtapiCapacity *cap)
{
    int tries;
    int started = 0;
    int ret = ATAPI_ERR_IO;
    u8 last_asc = 0, last_ascq = 0;

    for (tries = 0; tries < ATAPI_READY_RETRIES; tries++) {
        u8 sk = ATAPI_SK_NO_SENSE;
        u8 asc = 0;
        u8 ascq = 0;

        s_last_sense = ATAPI_SK_NO_SENSE;
        ret = atapi_capacity_raw(cap);
        if (ret != ATAPI_ERR_IO) return ret;
        if (s_last_sense == ATAPI_SK_UNIT_ATTENTION) {
            (void)atapi_request_sense(&sk, &asc, &ascq);
            s_stats.ready_retries++;
            continue;
        }
        if (s_last_sense == ATAPI_SK_NOT_READY) {
            if (atapi_request_sense(&sk, &asc, &ascq) == ATAPI_OK
                && asc == ATAPI_ASC_MEDIUM_NOT_PRESENT) {
                return ATAPI_ERR_NO_MEDIA;
            }
            last_asc = asc;
            last_ascq = ascq;
            s_stats.ready_retries++;
            ret = ATAPI_ERR_NO_MEDIA;
            if (asc == ATAPI_ASC_BECOMING_READY
                && ascq == ATAPI_ASCQ_INIT_CMD_REQUIRED && !started) {
                started = 1;
                atapi_note("NOT READY, START UNIT", 1, asc, ascq);
                (void)atapi_start_unit();
                continue;
            }
            atapi_delay_us(ATAPI_READY_WAIT_US);
            continue;
        }
        return ret;
    }
    if (ret == ATAPI_ERR_NO_MEDIA) atapi_note("NOT READY, gave up", 1, last_asc, last_ascq);
    return ret;
}

/* 選んでいる装置に媒体が入っているか (容量が読めて 1 セクタより多い) */
static int atapi_has_media(void)
{
    AtapiCapacity cap;
    return (atapi_capacity_ready(&cap) == ATAPI_OK && cap.total_sectors > 1) ? 1 : 0;
}

int atapi_init(void)
{
    static const u8 sels[2] = { 0x00, ATAPI_DRV_SLAVE };
    u8 found[2] = { 0, 0 };
    int n = 0;
    int i;

    /* 起動の最悪時間を抑える短い上限 (atapi.h の ATAPI_INIT_TIMEOUT_US)。
     * 戻る前に ATAPI_CMD_TIMEOUT_US に戻す */
    s_wait_limit_us = ATAPI_INIT_TIMEOUT_US;
    /* 呼び直し (バスの再試行) のとき前回の結果を持ち越さない。バスが死んだ印も
     * ここでだけ解く */
    cdrom_present = 0;
    s_bus_dead = 0;
    s_dead_noted = 0;

    /* セカンダリバンクに切替 */
    atapi_select_bank(1);

    /* 割り込み無効 (ポーリングモード) */
    outp(IDE_DEV_CTRL, IDE_NIEN);

    /* マスターとスレーブの両方を見る (2026-09-26: NP21/W の ide3 = セカンダリの
     * スレーブに ISO を付けると、マスターの空の CD ドライブだけを見ていて
     * 1 セクタも読めなかった) */
    s_present_mask = 0;
    for (i = 0; i < 2; i++) {
        if (atapi_probe_sig(sels[i])) {
            found[n++] = sels[i];
            s_present_mask |= (u8)(1 << i);
        }
    }

    if (n == 0 && atapi_srst() == ATAPI_OK) {
        /* シグネチャが出ない場合、ソフトリセット後に再確認。SRST の後も
         * マスターが BSY のまま (31 秒) なら諦める (BSY の装置が選ばれている
         * あいだは DRV_HEAD を書いても届かない) */
        for (i = 0; i < 2; i++) {
            if (atapi_probe_sig(sels[i])) {
                found[n++] = sels[i];
                s_present_mask |= (u8)(1 << i);
            }
        }
    }

    if (n > 0) {
        cdrom_present = 1;
        s_drvsel = found[0];
        /* 2 台あれば、媒体の入っている方 (マスター優先)。どちらも空ならマスター。
         * 容量を読むのはシグネチャを全部見た後 (PACKET で CylLo/Hi が変わる)。
         * 装置の選び直しと待ちは PACKET の共通路 (atapi_select_device) が
         * 容量確認ごとに行う */
        if (n == 2) {
            for (i = 0; i < n; i++) {
                s_drvsel = found[i];
                if (atapi_has_media()) break;
            }
            if (i == n) s_drvsel = found[0];
        }
    }

    /* バスの選択はそのまま (以後のコマンドは出す直前に選び直す) */
    /* プライマリバンクに戻す */
    atapi_select_bank(0);
    s_wait_limit_us = ATAPI_CMD_TIMEOUT_US;

    return cdrom_present;
}

/* 使っている装置 (0 = セカンダリのマスター、1 = スレーブ) */
int atapi_drive_index(void)
{
    return (s_drvsel == ATAPI_DRV_SLAVE) ? 1 : 0;
}

int atapi_present(void)
{
    return cdrom_present;
}

int atapi_test_unit_ready(void)
{
    u8 cdb[12];
    int ret;

    if (!cdrom_present) return ATAPI_ERR_NO_DRIVE;
    if (atapi_bus_dead_fail()) return ATAPI_ERR_TIMEOUT;

    atapi_select_bank(1);

    atapi_clear_cdb(cdb);
    cdb[0] = SCSI_CMD_TEST_UNIT_READY;

    ret = atapi_packet_nodata(cdb);

    atapi_select_bank(0);

    if (ret != ATAPI_OK) return ATAPI_ERR_NO_MEDIA;
    return ATAPI_OK;
}

int atapi_read_capacity(AtapiCapacity *cap)
{
    int ret;

    if (!cdrom_present) return ATAPI_ERR_NO_DRIVE;
    if (!cap) return ATAPI_ERR_IO;
    if (atapi_bus_dead_fail()) return ATAPI_ERR_TIMEOUT;

    atapi_select_bank(1);
    ret = atapi_capacity_ready(cap);
    atapi_select_bank(0);
    return ret;
}

/* READ(10) を 1 回出す (n セクタ、バンクは呼び手が選んでおく)。
 * デバイスが渡したバイト数がちょうど n セクタでなければ失敗。
 * UNIT ATTENTION なら媒体の世代を進めて ATAPI_UA_RETRIES 回まで出し直す
 * (UA を複数積む装置がある — リセットの後に媒体交換、など)。 */
static int atapi_read10(u32 lba, u32 n, u8 *dst)
{
    int attempt;
    int ret = ATAPI_ERR_IO;

    for (attempt = 0; attempt <= ATAPI_UA_RETRIES; attempt++) {
        u8 cdb[12];
        u32 got = 0;

        atapi_clear_cdb(cdb);
        cdb[0] = SCSI_CMD_READ_10;
        /* LBA (ビッグエンディアン, bytes 2-5) */
        cdb[2] = (u8)(lba >> 24);
        cdb[3] = (u8)(lba >> 16);
        cdb[4] = (u8)(lba >> 8);
        cdb[5] = (u8)(lba & 0xFF);
        /* 転送セクタ数 (ビッグエンディアン, bytes 7-8) */
        cdb[7] = (u8)(n >> 8);
        cdb[8] = (u8)(n & 0xFF);

        s_last_sense = 0;
        s_stats.read10_cmds++;
        ret = atapi_packet_read(cdb, dst, n * ATAPI_SECTOR_SIZE, &got);
        if (ret == ATAPI_OK && got != n * ATAPI_SECTOR_SIZE) ret = ATAPI_ERR_IO;
        if (ret == ATAPI_OK) {
            s_stats.read10_sectors += n;
            return ATAPI_OK;
        }
        /* 落ちたコマンドの引数 (診断の行は最後に落ちたコマンドとその時点の状態) */
        s_diag_lba = lba;
        s_diag_n = n;
        if (s_last_sense != ATAPI_SK_UNIT_ATTENTION) break;
    }
    return ret;
}

/* 読みが失敗したときの 1 行 (ATAPI_DIAG_MAX 行まで)。NP21/W と実機で
 * 「どこで落ちたか」を画面で分けるため。lba / n は**最後に落ちた READ(10)**
 * (複数セクタが落ちて 1 セクタずつ読み直したなら、その 1 セクタ) で、
 * st / err / sense / got もそのコマンドの終わった時点の値。req は呼び手の範囲:
 *   sense=5 (ILLEGAL REQUEST) = 範囲外か、ドライブに媒体が無い (NP21/W は
 *            空のドライブへの READ(10) を ILLEGAL REQUEST / asc 21h で返す)
 *   sense=2 = NOT READY、sense=6 = UNIT ATTENTION (出し直しても落ちた)
 *   ret=-1 = BSY / DRQ の待ちの期限切れ (st はそのときの ALT_STATUS)、
 *   got が n*2048 未満 = 転送が足りない (期限切れならそこまで受け取った数) */
static void atapi_diag_fail(u32 req_lba, u32 req_n, int ret)
{
    if (s_diag_lines >= ATAPI_DIAG_MAX) return;
    s_diag_lines++;
    kprintf(0x07, "[atapi] READ(10) drv=%d lba=%u n=%u ret=%d st=%02x err=%02x sense=%x got=%u req=%u+%u\n",
            atapi_drive_index(), (unsigned)s_diag_lba, (unsigned)s_diag_n, ret,
            (unsigned)s_diag_st, (unsigned)s_diag_err, (unsigned)s_last_sense,
            (unsigned)s_diag_got, (unsigned)req_lba, (unsigned)req_n);
}

int atapi_read_sectors(u32 lba, u32 count, void *buf)
{
    u8 *p = (u8 *)buf;

    if (!cdrom_present) return ATAPI_ERR_NO_DRIVE;
    if (atapi_bus_dead_fail()) return ATAPI_ERR_TIMEOUT;

    atapi_select_bank(1);

    /* 連続する範囲を ATAPI_READ_MAX_SECTORS ずつの READ(10) で読む */
    while (count > 0) {
        u32 n = (count < ATAPI_READ_MAX_SECTORS) ? count : ATAPI_READ_MAX_SECTORS;
        int ret = atapi_read10(lba, n, p);

        /* 複数セクタが失敗したら、その範囲を 1 セクタずつ読み直す
         * (ドライブとの相性・範囲のどこかの読めないセクタ)。
         * 1 セクタでも落ちたらそこで失敗を返す */
        if (ret != ATAPI_OK && n > 1) {
            u32 i;
            s_stats.multi_fail++;
            for (i = 0; i < n; i++) {
                s_stats.single_retry++;
                ret = atapi_read10(lba + i, 1, p + i * ATAPI_SECTOR_SIZE);
                if (ret != ATAPI_OK) break;
            }
        }
        if (ret != ATAPI_OK) {
            atapi_select_bank(0);
            atapi_diag_fail(lba, n, ret);
            return ret;
        }
        lba   += n;
        count -= n;
        p     += n * ATAPI_SECTOR_SIZE;
    }

    atapi_select_bank(0);
    return ATAPI_OK;
}

u32 atapi_media_gen(void)
{
    return s_media_gen;
}

void atapi_get_stats(AtapiStats *out)
{
    if (out) *out = s_stats;
}
