#include "os32_kapi_shared.h"
#include "con_sink.h"
#include "selftest_internal.h"

/* ホスト fixture は所有者とデータの SETTER だけを自前でつなぐ。 */
#ifdef KDEBUG_SELFTEST_HOST
extern int res_owner_get(void);
extern void res_owner_set(int owner);
extern void kdi_set_con_sink_drop_count(u32 value);
#else
#include "generated/kdebug_import_macros.h"
#endif

/* ------------------------------------------------------------------------ */
/*  自己診断 (kdebug/kselftest.c から、ブート時に 1 回)                      */
/*                                                                          */
/*  票 K6C の受入 C2。実機で毎回踏むのは「レコード境界」「あふれで古い方を   */
/*  捨てる」「CUI 復帰で破棄」「読み手の所有」— どれもホスト試験で固めた     */
/*  形だが、8KB の環と割込み禁止区間はカーネル帯でしか確かめられない。       */
/*  戻り値はビットマスク (0 = 全部通った)。終わった時点でリングは空・無効。  */
/* ------------------------------------------------------------------------ */

static u8 g_self_buf[CON_SINK_REC_MAX];
static u8 g_self_src[CON_SINK_PRINT_MAX];

u32 kst_con_sink(void)
{
    u32 bad = 0;
    u32 pending = 0;
    u32 dropped0 = 0;
    u32 dropped1 = 0;
    i32 n;
    u32 i;
    int saved_owner = res_owner_get();

    for (i = 0; i < (u32)CON_SINK_PRINT_MAX; i++) {
        g_self_src[i] = (u8)('a' + (i & 15));
    }

    /* (0) push した 1 本がそのままの形で読み出せる */
    con_sink_enable();
    con_sink_push_print("AB", 2, (u8)ATTR_WHITE);
    con_sink_stat(&pending, &dropped0);
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (pending != (u32)CON_SINK_HDR_PRINT + 2u ||
        n != (i32)((u32)CON_SINK_HDR_PRINT + 2u) ||
        g_self_buf[0] != (u8)CON_SINK_REC_PRINT ||
        g_self_buf[1] != (u8)ATTR_WHITE ||
        g_self_buf[2] != 2 ||
        g_self_buf[3] != 'A' || g_self_buf[4] != 'B') {
        bad |= 1u << 0;
    }
    if (con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX) != 0) bad |= 1u << 0;

    /* (1) CLEAR / CURSOR */
    con_sink_push_clear();
    con_sink_push_cursor(12, 34);
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (n != (i32)((u32)CON_SINK_HDR_CLEAR + (u32)CON_SINK_HDR_CURSOR) ||
        g_self_buf[0] != (u8)CON_SINK_REC_CLEAR ||
        g_self_buf[1] != (u8)CON_SINK_REC_CURSOR ||
        g_self_buf[2] != 12 || g_self_buf[3] != 34) {
        bad |= 1u << 1;
    }

    /* (2) レコード境界で切る: 最大長 2 本は 1 回で 1 本しか入らない */
    con_sink_push_print((const char *)g_self_src, (u32)CON_SINK_PRINT_MAX, 1);
    con_sink_push_print((const char *)g_self_src, (u32)CON_SINK_PRINT_MAX, 2);
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (n != (i32)CON_SINK_REC_MAX || g_self_buf[1] != 1) bad |= 1u << 2;
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (n != (i32)CON_SINK_REC_MAX || g_self_buf[1] != 2) bad |= 1u << 2;
    if (con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX) != 0) bad |= 1u << 2;

    /* (3) あふれ: 8KB を超えて積むと古い方がレコード単位で消える。色を
     * 通し番号にしておき、残った先頭が「最初に積んだ 1 本」ではないことと、
     * 溜まりが容量を超えないことを見る。 */
    con_sink_stat(&pending, &dropped0);
    for (i = 0; i < 64; i++) {
        con_sink_push_print((const char *)g_self_src, (u32)CON_SINK_PRINT_MAX,
                            (u8)(i + 1));
    }
    con_sink_stat(&pending, &dropped1);
    if (dropped1 <= dropped0 || pending > (u32)CON_SINK_RING_SIZE) bad |= 1u << 3;
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (n != (i32)CON_SINK_REC_MAX || g_self_buf[1] == 1) bad |= 1u << 3;

    /* (4) CUI 復帰で破棄 → 無効中は積まない → 再入場は空から */
    con_sink_disable();
    con_sink_stat(&pending, &dropped0);
    if (pending != 0) bad |= 1u << 4;
    con_sink_push_print("x", 1, 1);
    con_sink_stat(&pending, &dropped0);
    if (pending != 0) bad |= 1u << 4;
    con_sink_enable();
    con_sink_stat(&pending, &dropped0);
    if (pending != 0) bad |= 1u << 4;

    /* (5) 読み手は 1 本。owner 回収で次の 1 本が読めるようになる。 */
    con_sink_owner_exit(saved_owner);       /* (0)〜(3) で握った所有を返す */
    res_owner_set(2);
    if (con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX) != 0) bad |= 1u << 5;
    res_owner_set(3);
    if (con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX) != (i32)OS32_ERR_EXIST) {
        bad |= 1u << 5;
    }
    con_sink_owner_exit(2);
    if (con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX) != 0) bad |= 1u << 5;
    con_sink_owner_exit(3);
    res_owner_set(saved_owner);

    /* (6) EXIT レコード (票 T7 E1)。型から長さが導けないと、あふれたとき
     * バイト単位で捨てて環が半端な位置から読まれる — だから境界を見る。 */
    con_sink_enable();
    con_sink_push_exit(6);
    con_sink_push_cursor(1, 2);
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (n != (i32)((u32)CON_SINK_HDR_EXIT + (u32)CON_SINK_HDR_CURSOR) ||
        g_self_buf[0] != (u8)CON_SINK_REC_EXIT ||
        g_self_buf[1] != 6 ||
        g_self_buf[2] != (u8)CON_SINK_REC_CURSOR ||
        g_self_buf[3] != 1 || g_self_buf[4] != 2) {
        bad |= 1u << 6;
    }
    kdi_set_con_sink_drop_count(0);
    for (i = 0; i < (u32)CON_SINK_RING_SIZE; i++) con_sink_push_exit(7);
    con_sink_stat(&pending, &dropped0);
    if (pending > (u32)CON_SINK_RING_SIZE ||
        pending % (u32)CON_SINK_HDR_EXIT != 0) {
        bad |= 1u << 6;
    }
    n = con_sink_read(g_self_buf, (u32)CON_SINK_REC_MAX);
    if (n <= 0 || n % (i32)CON_SINK_HDR_EXIT != 0 ||
        g_self_buf[0] != (u8)CON_SINK_REC_EXIT ||
        g_self_buf[(u32)CON_SINK_HDR_EXIT] != (u8)CON_SINK_REC_EXIT) {
        bad |= 1u << 6;
    }
    con_sink_owner_exit(saved_owner);

    /* 後始末: CUI に戻す (ブートの続きはテキスト面が正)。 */
    con_sink_disable();
    kdi_set_con_sink_drop_count(0);
    return bad;
}
