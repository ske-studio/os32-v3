/* ======================================================================== */
/*  ne2000_ring_test.c — drivers/ne2000_ring.c のホスト試験                  */
/*                                                                          */
/*  make check (check-ne2000-ring) がホスト gcc でビルドして実行する。        */
/*  I/O の実動作試験を代替するものではない (docs/tasks/network/PLAN.md §6)。  */
/*  検査する境界: 256 バイトページ境界前後、PSTOP wrap、最短/最大長、         */
/*  実カード (count に FCS を含む) と NP21/W (含まない) の両方のヘッダ形式。  */
/* ======================================================================== */

#include <stdio.h>
#include "ne2000_ring.h"

#define PSTART 0x46
#define PSTOP  0x80

static int fails = 0;
static int total = 0;

static void expect(int cond, const char *what)
{
    total++;
    if (!cond) {
        fails++;
        printf("FAIL: %s\n", what);
    }
}

/* 実カード形式: count = 4 + data + 4 (FCS)、next = cur + ceil(count / 256) */
static void hdr_real(struct ne2k_rx_hdr *h, u8 cur, unsigned int data, u8 status)
{
    unsigned int count = 4 + data + 4;
    h->status = status;
    h->count  = (u16)count;
    h->next   = ne2k_ring_advance(PSTART, PSTOP, cur, (u8)((count + 255) / 256));
}

/* NP21/W 形式 (lgy98.c ne2000_receive): count = data + 4、
 * next = cur + ((count + 4 + 255) >> 8) */
static void hdr_np21w(struct ne2k_rx_hdr *h, u8 cur, unsigned int data, u8 status)
{
    unsigned int count = data + 4;
    h->status = status;
    h->count  = (u16)count;
    h->next   = ne2k_ring_advance(PSTART, PSTOP, cur, (u8)((count + 4 + 255) >> 8));
}

static void test_primitives(void)
{
    u8 raw[4] = { 0x21, 0x4B, 0x34, 0x12 };
    struct ne2k_rx_hdr h;

    ne2k_ring_decode_hdr(raw, &h);
    expect(h.status == 0x21 && h.next == 0x4B && h.count == 0x1234, "decode_hdr");

    expect(ne2k_ring_pages_for(68) == 1,   "pages_for 68");
    expect(ne2k_ring_pages_for(256) == 1,  "pages_for 256");
    expect(ne2k_ring_pages_for(257) == 2,  "pages_for 257");
    expect(ne2k_ring_pages_for(1522) == 6, "pages_for 1522");

    expect(ne2k_ring_advance(PSTART, PSTOP, 0x50, 1) == 0x51, "advance +1");
    expect(ne2k_ring_advance(PSTART, PSTOP, 0x7F, 1) == PSTART, "advance wrap to pstart");
    expect(ne2k_ring_advance(PSTART, PSTOP, 0x7E, 3) == 0x47, "advance wrap +3");
    expect(ne2k_ring_advance(PSTART, PSTOP, 0x7F, 6) == 0x4B, "advance wrap max frame");

    expect(ne2k_ring_bnry_for(PSTART, PSTOP, 0x50) == 0x4F, "bnry_for mid");
    expect(ne2k_ring_bnry_for(PSTART, PSTOP, PSTART) == PSTOP - 1, "bnry_for wrap");

    expect(ne2k_ring_first_chunk(PSTOP, 0x4604, 100) == 100, "first_chunk no split");
    expect(ne2k_ring_first_chunk(PSTOP, 0x7F04, 1514) == 252, "first_chunk split");
    expect(ne2k_ring_first_chunk(PSTOP, 0x7FFE, 2) == 2, "first_chunk exact end");
    expect(ne2k_ring_first_chunk(PSTOP, 0x7FFE, 3) == 2, "first_chunk one over");
}

static void test_check_formats(void)
{
    static const unsigned int sizes[] = {
        60, 61, 100, 247, 248, 249, 250, 251, 252, 253, 254, 255, 256,
        500, 1000, 1500, 1513, 1514
    };
    unsigned int i;
    struct ne2k_rx_hdr h;
    u16 dlen;
    int rc;
    char msg[64];

    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        /* 実カード形式、途中ページから */
        hdr_real(&h, 0x50, sizes[i], 0x01);
        rc = ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen);
        sprintf(msg, "real size %u", sizes[i]);
        expect(rc == NE2K_RING_OK && dlen == sizes[i], msg);

        /* NP21/W 形式 */
        hdr_np21w(&h, 0x50, sizes[i], 0x01);
        rc = ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 0, &dlen);
        sprintf(msg, "np21w size %u", sizes[i]);
        expect(rc == NE2K_RING_OK && dlen == sizes[i], msg);

        /* PSTOP をまたぐ位置 (実カード形式) */
        hdr_real(&h, 0x7F, sizes[i], 0x01);
        rc = ne2k_ring_check(PSTART, PSTOP, 0x7F, &h, 1, &dlen);
        sprintf(msg, "real wrap size %u", sizes[i]);
        expect(rc == NE2K_RING_OK && dlen == sizes[i], msg);

        /* PSTOP をまたぐ位置 (NP21/W 形式) */
        hdr_np21w(&h, 0x7E, sizes[i], 0x01);
        rc = ne2k_ring_check(PSTART, PSTOP, 0x7E, &h, 0, &dlen);
        sprintf(msg, "np21w wrap size %u", sizes[i]);
        expect(rc == NE2K_RING_OK && dlen == sizes[i], msg);
    }
}

static void test_check_errors(void)
{
    struct ne2k_rx_hdr h;
    u16 dlen = 0xFFFF;
    int rc;

    /* next が範囲外 */
    hdr_real(&h, 0x50, 100, 0x01);
    h.next = 0x45;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_NEXT, "next below pstart");
    h.next = PSTOP;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_NEXT, "next == pstop");
    h.next = 0x50;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_NEXT, "self reference");

    /* 長さが範囲外 */
    hdr_real(&h, 0x50, 100, 0x01);
    h.count = 4 + 59 + 4;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_COUNT, "runt (real)");
    h.count = 4 + 1515 + 4;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_COUNT, "oversize (real)");
    hdr_np21w(&h, 0x50, 100, 0x01);
    h.count = 4 + 59;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 0, &dlen) == NE2K_RING_BAD_COUNT, "runt (np21w)");
    h.count = 0;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 0, &dlen) == NE2K_RING_BAD_COUNT, "zero count");

    /* ページ進行と矛盾 (count は 1 ページなのに next が 2 ページ先) */
    hdr_real(&h, 0x50, 100, 0x01);
    h.next = 0x52;
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_COUNT, "pages mismatch (+1)");
    hdr_real(&h, 0x50, 1514, 0x01);
    h.next = 0x55;      /* 6 ページ必要なのに 5 */
    expect(ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen) == NE2K_RING_BAD_COUNT, "pages mismatch (-1)");

    /* 形式の取り違え: 実カード形式のヘッダを NP21/W として読むと 4 バイト過大 */
    hdr_real(&h, 0x50, 100, 0x01);
    rc = ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 0, &dlen);
    expect(rc == NE2K_RING_OK && dlen == 104, "format mismatch is not detectable by geometry (documented)");

    /* エラーフレーム: 幾何は妥当、status に PRX 無し */
    hdr_real(&h, 0x50, 100, 0x02);
    rc = ne2k_ring_check(PSTART, PSTOP, 0x50, &h, 1, &dlen);
    expect(rc == NE2K_RING_RX_ERROR && dlen == 100, "rx error keeps next usable");
}

int main(void)
{
    test_primitives();
    test_check_formats();
    test_check_errors();
    printf("ne2000_ring_test: %d/%d passed\n", total - fails, total);
    return fails ? 1 : 0;
}
