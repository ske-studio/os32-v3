/* ======================================================================== */
/*  DMA_POOL_HOST.C — kernel/dma_pool_math.c をそのままホストで回す         */
/*                                                                          */
/*  表の算数だけを見る。実物を 1 行も写さずに #include する                 */
/*  (64KB またぎの規則は drivers/dma8237_math.c の dma_crosses_64k が正典   */
/*  なので、そちらも同じ実行ファイルに入れる)。                             */
/*                                                                          */
/*  **NP21/W では踏めない**のが 64KB またぎの飛ばし — エミュレータは 8237   */
/*  の折り返しを模擬しないので、またいだ配置でも「動いて見える」。          */
/*  番地は実物の MEM_DMA_POOL_BASE / _SIZE をそのまま使う。                 */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/dma8237_math.c"
#include "../../kernel/dma_pool_math.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* 期待値は**数で書く** ([C4] の写しではなく、設計の数そのもの)。 */
#define POOL_BASE   0x2E8000UL
#define POOL_END    0x2F7FFFUL
#define POOL_BANK   0x2F0000UL   /* 池が跨ぐ 64KB 境界 */
#define KB(n)       ((u32)(n) * 1024UL)

static struct dma_pool_state P;

static void fresh(void)
{
    memset(&P, 0, sizeof(P));
    dma_pool_state_init(&P, (u32)MEM_DMA_POOL_BASE, DMA_POOL_PAGES);
}

static int last_rc;

static u32 A(u32 bytes, u32 align)
{
    u32 out = 0xDEADBEEFUL;
    last_rc = dma_pool_state_alloc(&P, bytes, align, DMA_PHYS_LIMIT, &out);
    return (last_rc == 0) ? out : 0;
}

/* state が s の span の本数。**表そのもの**を見る白箱の検査で、
 * 「番地が返るかどうか」では見えない食い違い (LEAKED を FREE に戻す等) を
 * 拾うために要る。 */
static int spans_in(u8 state)
{
    int i, n = 0;
    for (i = 0; i < DMA_POOL_SPANS; i++) {
        if (P.span[i].state == state) n++;
    }
    return n;
}

/* ------------------------------------------------------------------ */
/*  設計の数そのもの                                                   */
/* ------------------------------------------------------------------ */
static void layout(void)
{
    CHECK((u32)MEM_DMA_POOL_BASE == POOL_BASE);
    CHECK((u32)MEM_DMA_POOL_SIZE == KB(64));
    CHECK((u32)MEM_DMA_POOL_END == POOL_END);
    CHECK(DMA_POOL_PAGES == 16);
    /* **池は 64KB 境界を跨ぐ**。跨がない池なら以下の試験は全部素通し
     * なので、跨いでいること自体をここで固定する。 */
    CHECK(POOL_BASE < POOL_BANK && POOL_BANK <= POOL_END);
    /* 予約域 (カーネルスタックガード 0x2FB000) を越えない。 */
    CHECK(POOL_END < 0x2FB000UL);
    /* 受付上限は境界の片側ぶん。 */
    CHECK(DMA_POOL_MAX_BYTES == KB(32));
    CHECK(POOL_BANK - POOL_BASE == KB(32));
    CHECK(POOL_END + 1 - POOL_BANK == KB(32));
}

/* ------------------------------------------------------------------ */
/*  空の池に必ず入るもの / 絶対に入らないもの                          */
/* ------------------------------------------------------------------ */
static void sizes(void)
{
    u32 a, b;

    /* 16KB は空の池なら必ず入り、0x2F0000 を跨がない。 */
    fresh();
    a = A(KB(16), 0);
    CHECK(a == POOL_BASE);
    CHECK(dma_crosses_64k(a, KB(16)) == 0);
    CHECK(a + KB(16) - 1 < POOL_BANK);

    /* **33KB は空の池でも必ず失敗する** — 境界の両側がそれぞれ 32KB。
     * 「断片化のせい」と読まれないよう入口で断る。 */
    fresh();
    CHECK(A(KB(33), 0) == 0);
    /* **入口で断る** (ERR_ARG) — 「空きが無い」(ERR_NOSPC) ではない。
     * 呼び手が「断片化したから後で再試行」と読まないように分ける。 */
    CHECK(last_rc == DMA_POOL_ERR_ARG);
    CHECK(A(KB(64), 0) == 0);
    CHECK(last_rc == DMA_POOL_ERR_ARG);
    CHECK(A(KB(32) + 1, 0) == 0);
    CHECK(last_rc == DMA_POOL_ERR_ARG);
    CHECK(A(0, 0) == 0);
    CHECK(last_rc == DMA_POOL_ERR_ARG);
    /* 32KB ちょうどは通る (境界の手前ぴったり)。 */
    CHECK(A(KB(32), 0) == POOL_BASE);

    /* **32KB × 2 が空の池に入る** (往復 8 の具体例)。 */
    fresh();
    a = A(KB(32), 0);
    b = A(KB(32), 0);
    CHECK(a == POOL_BASE);
    CHECK(b == POOL_BANK);
    CHECK(dma_crosses_64k(a, KB(32)) == 0);
    CHECK(dma_crosses_64k(b, KB(32)) == 0);
    CHECK(A(KB(4), 0) == 0);          /* もう空きが無い */

    /* 0 バイトと範囲外の整列は断る。 */
    fresh();
    CHECK(A(0, 0) == 0);
    CHECK(A(KB(4), 3) == 0);          /* 2 の冪でない */
    CHECK(A(KB(4), KB(64)) == 0);     /* 上限超え */
    CHECK(A(1, 0) == POOL_BASE);      /* 1 バイトは 1 ページ */
}

/* ------------------------------------------------------------------ */
/*  64KB 境界を跨ぐ候補を飛ばして後半に置く (往復 8 の具体例)          */
/* ------------------------------------------------------------------ */
static void skip_boundary(void)
{
    u32 a, b;

    fresh();
    /* 28KB (7 ページ) = 0x2E8000〜0x2EEFFF */
    a = A(KB(28), 0);
    CHECK(a == POOL_BASE);
    /* 次の 16KB。最初適合なら 0x2EF000 だが、そこは 0x2F0000 を跨ぐので
     * **飛ばして** 0x2F0000 に置く。「跨ぐから諦める」ではない。 */
    b = A(KB(16), 0);
    CHECK(b == POOL_BANK);
    CHECK(dma_crosses_64k(b, KB(16)) == 0);
    /* 飛ばした 1 ページ (0x2EF000) はまだ空いていて、4KB なら入る。 */
    CHECK(A(KB(4), 0) == 0x2EF000UL);
}

/* ------------------------------------------------------------------ */
/*  整列                                                               */
/* ------------------------------------------------------------------ */
static void alignment(void)
{
    u32 a, b;

    fresh();
    CHECK(A(KB(4), 0) == POOL_BASE);         /* 既定は 4KB */
    /* 次は 8KB 整列を頼む。0x2E9000 は 8KB 整列ではないので 0x2EA000。 */
    a = A(KB(4), KB(8));
    CHECK(a == 0x2EA000UL);
    CHECK(a % KB(8) == 0);
    /* 32KB 整列は池の中に 2 か所しかない (0x2E8000 と 0x2F0000)。
     * 前者は埋まっているので後者。 */
    b = A(KB(4), KB(32));
    CHECK(b == POOL_BANK);
    CHECK(b % KB(32) == 0);
}

/* ------------------------------------------------------------------ */
/*  隣り合う span の解放・途中ポインタ・二重解放                       */
/* ------------------------------------------------------------------ */
static void frees(void)
{
    u32 a, b, c, d;

    fresh();
    a = A(KB(16), 0);   /* 0x2E8000  4 ページ */
    b = A(KB(16), 0);   /* 0x2EC000  4 ページ */
    c = A(KB(16), 0);   /* 0x2F0000  4 ページ (0x2EF000 は跨ぐので飛ぶ) */
    CHECK(a == POOL_BASE);
    CHECK(b == 0x2EC000UL);
    CHECK(c == POOL_BANK);

    /* **真ん中だけ**を返す。隣が生きているので、先頭一致でなければ
     * どれを外すか決まらない。 */
    CHECK(dma_pool_state_free(&P, b) == 0);
    CHECK(P.bad_free == 0);
    /* 返した 16KB の中に 8KB が入る (前後は埋まったまま)。 */
    d = A(KB(8), 0);
    CHECK(d == 0x2EC000UL);

    /* 途中ポインタ: span の先頭ではないので断り、bad_free を数える。 */
    CHECK(dma_pool_state_free(&P, a + KB(4)) < 0);
    CHECK(P.bad_free == 1);
    /* ページ境界ですらない番地 */
    CHECK(dma_pool_state_free(&P, a + 16) < 0);
    CHECK(P.bad_free == 2);
    /* 池の外 */
    CHECK(dma_pool_state_free(&P, POOL_BASE - KB(4)) < 0);
    CHECK(dma_pool_state_free(&P, POOL_END + 1) < 0);
    CHECK(P.bad_free == 4);

    /* 二重解放 */
    CHECK(dma_pool_state_free(&P, a) == 0);
    CHECK(dma_pool_state_free(&P, a) < 0);
    CHECK(P.bad_free == 5);

    /* **隣接の曖昧さ**: a を返した後、b の隣 (0x2EE000) を渡しても
     * span の先頭ではないので通らない。 */
    CHECK(dma_pool_state_free(&P, 0x2EE000UL) < 0);
    CHECK(P.bad_free == 6);

    CHECK(dma_pool_state_free(&P, c) == 0);
}

/* ------------------------------------------------------------------ */
/*  LEAKED は二度と配らない                                            */
/* ------------------------------------------------------------------ */
static void leaked(void)
{
    u32 a, b, i;

    fresh();
    a = A(KB(32), 0);
    CHECK(a == POOL_BASE);
    /* 装置が止まった証拠が取れなかった。**返さずに印を付ける**。 */
    CHECK(dma_pool_state_mark_leaked(&P, a) == 0);
    CHECK(P.leaked == 1);
    /* **span は LEAKED のまま残る。** FREE に戻すと表の枠が再利用され、
     * 「まだ装置が書いているかもしれない」印が消える。 */
    CHECK(spans_in(DMA_SPAN_LEAKED) == 1);
    CHECK(spans_in(DMA_SPAN_USED) == 0);

    /* LEAKED を free しても空きに戻らない (往復 8 の具体例)。 */
    CHECK(dma_pool_state_free(&P, a) < 0);
    CHECK(P.bad_free == 1);

    /* 残りの 32KB だけが配られる。**LEAKED の番地は二度と出てこない**。 */
    for (i = 0; i < 8; i++) {
        b = A(KB(4), 0);
        CHECK(b >= POOL_BANK);
        CHECK(b < a + KB(32) ? b >= POOL_BANK : 1);
    }
    CHECK(A(KB(4), 0) == 0);   /* 枯渇 */

    /* 二重 mark も数える。 */
    fresh();
    a = A(KB(8), 0);
    CHECK(dma_pool_state_mark_leaked(&P, a) == 0);
    CHECK(dma_pool_state_mark_leaked(&P, a) < 0);
    CHECK(P.bad_free == 1);
    CHECK(P.leaked == 1);
    CHECK(spans_in(DMA_SPAN_LEAKED) == 1);
}

/* ------------------------------------------------------------------ */
/*  枯渇 → 解放 → 再利用 / 初期化前                                    */
/* ------------------------------------------------------------------ */
static void exhaust(void)
{
    u32 got[DMA_POOL_PAGES];
    u32 i, n = 0;

    fresh();
    for (i = 0; i < DMA_POOL_PAGES; i++) {
        got[i] = A(KB(4), 0);
        if (got[i]) n++;
    }
    CHECK(n == DMA_POOL_PAGES);          /* 16 ページ全部出る */
    CHECK(A(KB(4), 0) == 0);             /* 17 本目は無い */

    /* span 表は 16 本ちょうど。全部使っても足りる。 */
    CHECK(DMA_POOL_SPANS == DMA_POOL_PAGES);

    CHECK(dma_pool_state_free(&P, got[5]) == 0);
    CHECK(A(KB(4), 0) == got[5]);        /* 返したところが再び出る */

    /* 初期化前は全部断る (ready = 0)。**ERR_STATE であって ERR_NOSPC では
     * ない** — 「空きが無い」と読まれると呼び手が再試行を回してしまう。 */
    memset(&P, 0, sizeof(P));
    CHECK(A(KB(4), 0) == 0);
    CHECK(last_rc == DMA_POOL_ERR_STATE);
    CHECK(dma_pool_state_free(&P, POOL_BASE) == DMA_POOL_ERR_STATE);
    CHECK(dma_pool_state_mark_leaked(&P, POOL_BASE) == DMA_POOL_ERR_STATE);
    CHECK(P.bad_free == 0);              /* 未初期化は「不正解放」に数えない */
}

/* ------------------------------------------------------------------ */
/*  T1c: 装置に渡してよい範囲 (dma_range_ok — FDC の BSS もこれで見る)  */
/* ------------------------------------------------------------------ */
static void range_ok(void)
{
    /* 終端がちょうど limit は通る、1 バイト越えは断る。 */
    CHECK(dma_range_ok(0xFFC000UL, KB(16), 0x1000000UL) == 1);
    CHECK(dma_range_ok(0xFFC000UL, KB(16) + 1, 0x1000000UL) == 0);
    /* **先頭だけでなく終端**を見る (0xFFF000 + 8KB)。 */
    CHECK(dma_range_ok(0xFFF000UL, KB(8), 0x1000000UL) == 0);
    CHECK(dma_range_ok(0x1000000UL, KB(4), 0x1000000UL) == 0);
    /* 64KB またぎは limit と無関係に断る。 */
    CHECK(dma_range_ok(POOL_BANK - KB(4), KB(8), 0x1000000UL) == 0);
    CHECK(dma_range_ok(POOL_BANK - KB(4), KB(4), 0x1000000UL) == 1);
    /* 0 バイトは断る、limit 0 は何も通さない。 */
    CHECK(dma_range_ok(POOL_BANK, 0, 0x1000000UL) == 0);
    CHECK(dma_range_ok(0, KB(4), 0) == 0);
    /* 引き算で見る: 巨大な size で終端が巻いても通さない。 */
    CHECK(dma_range_ok(KB(4), 0xFFFFFFFFUL, 0x1000000UL) == 0);
    /* 8237 の上限は 16MB。 */
    CHECK(DMA_PHYS_LIMIT == 0x1000000UL);
}

/* ------------------------------------------------------------------ */
/*  T1c: limit 未満にだけ置く                                          */
/* ------------------------------------------------------------------ */
static void limit(void)
{
    u32 base = (u32)MEM_DMA_POOL_BASE, out;

    /* 終端がちょうど limit なら通り、その先は無い。 */
    fresh();
    out = 0;
    CHECK(dma_pool_state_alloc(&P, KB(16), 0, base + KB(16), &out) == 0);
    CHECK(out == base);
    CHECK(dma_pool_state_alloc(&P, KB(4), 0, base + KB(16), &out) ==
          DMA_POOL_ERR_NOSPC);
    /* 先頭は limit の内側でも、終端が越えるなら断る。 */
    fresh();
    CHECK(dma_pool_state_alloc(&P, KB(32), 0, base + KB(16), &out) ==
          DMA_POOL_ERR_NOSPC);
    /* limit が池の先頭なら何も置けない。**表は触らない**。 */
    CHECK(dma_pool_state_alloc(&P, KB(4), 0, base, &out) ==
          DMA_POOL_ERR_NOSPC);
    CHECK(spans_in(DMA_SPAN_USED) == 0);
    /* 越える候補は**飛ばすだけ**: limit の内側の後ろの隙間には置ける。 */
    fresh();
    CHECK(A(KB(8), 0) == base);
    CHECK(dma_pool_state_alloc(&P, KB(8), 0, base + KB(16), &out) == 0);
    CHECK(out == base + KB(8));
    CHECK(out + KB(8) <= base + KB(16));
}

/* ------------------------------------------------------------------ */
/*  T1c: 失敗時 *out は不変 (内部の *out = 0 が漏れない)               */
/* ------------------------------------------------------------------ */
static int buf_is(const struct dma_buf *b, u32 pa, void *va, u32 size)
{
    return b->pa == pa && b->va == va && b->size == size;
}

static void keep_out(void)
{
    struct dma_buf b;
    void *sent = (void *)&b;
    u32 i;

    fresh();
    b.pa = 0xA5A5A000UL; b.va = sent; b.size = 0x77;
    /* 引数が悪い (33KB / 0 バイト / 整列) */
    CHECK(dma_pool_state_alloc_buf(&P, KB(33), 0, DMA_PHYS_LIMIT, &b) ==
          DMA_POOL_ERR_ARG);
    CHECK(buf_is(&b, 0xA5A5A000UL, sent, 0x77));
    CHECK(dma_pool_state_alloc_buf(&P, 0, 0, DMA_PHYS_LIMIT, &b) < 0);
    CHECK(buf_is(&b, 0xA5A5A000UL, sent, 0x77));
    CHECK(dma_pool_state_alloc_buf(&P, KB(4), 3, DMA_PHYS_LIMIT, &b) < 0);
    CHECK(buf_is(&b, 0xA5A5A000UL, sent, 0x77));
    /* limit を越える */
    CHECK(dma_pool_state_alloc_buf(&P, KB(4), 0, (u32)MEM_DMA_POOL_BASE, &b)
          == DMA_POOL_ERR_NOSPC);
    CHECK(buf_is(&b, 0xA5A5A000UL, sent, 0x77));
    /* 枯渇 */
    for (i = 0; i < DMA_POOL_PAGES; i++) CHECK(A(KB(4), 0) != 0);
    CHECK(dma_pool_state_alloc_buf(&P, KB(4), 0, DMA_PHYS_LIMIT, &b) ==
          DMA_POOL_ERR_NOSPC);
    CHECK(buf_is(&b, 0xA5A5A000UL, sent, 0x77));
    /* 初期化前 */
    memset(&P, 0, sizeof(P));
    CHECK(dma_pool_state_alloc_buf(&P, KB(4), 0, DMA_PHYS_LIMIT, &b) ==
          DMA_POOL_ERR_STATE);
    CHECK(buf_is(&b, 0xA5A5A000UL, sent, 0x77));
    /* out が NULL */
    fresh();
    CHECK(dma_pool_state_alloc_buf(&P, KB(4), 0, DMA_PHYS_LIMIT, 0) ==
          DMA_POOL_ERR_ARG);
    CHECK(spans_in(DMA_SPAN_USED) == 0);

    /* 成功は組で返る: va = P2V(pa)、size は要求のまま。 */
    fresh();
    CHECK(dma_pool_state_alloc_buf(&P, KB(16) - 100, 0, DMA_PHYS_LIMIT, &b)
          == 0);
    CHECK(b.pa == (u32)MEM_DMA_POOL_BASE);
    CHECK(b.va == P2V(b.pa));
    CHECK(b.size == KB(16) - 100);
    CHECK(dma_pool_state_free(&P, b.pa) == 0);
}

/* ------------------------------------------------------------------ */
/*  T1c / R4: PCM リング 16KB + 82557 ≒16KB の最悪の並び               */
/*                                                                    */
/*  **池の番地を焼かない**: 池の先頭を 64KB バンクの中の 16 通りの     */
/*  4KB 位置 (今の 0x2E8000 = 半ば、T3 の 0x3E0000 = 先頭を含む) に    */
/*  置いて全部で見る。前置きは 0 = 空 / 1 = 先頭に 8KB 使用中 /        */
/*  2 = 先頭に 8KB の穴 (次の 8KB は使用中)。順は PCM が先 / 82557 が   */
/*  先の両方。                                                          */
/* ------------------------------------------------------------------ */
#define PCM_BYTES  KB(16)   /* PCM_RING_BYTES */
#define NIC_BYTES  KB(16)   /* 82557 CB/RFD ≒16KB */
#define OVERLAP(a, an, b, bn)  ((a) < (b) + (bn) && (b) < (a) + (an))

static void worst(void)
{
    u32 k, pre, order, h1, h2, r, n, nb, rb;

    for (k = 0; k < 16; k++) {
        u32 base = 0x3E0000UL + k * KB(4);
        for (pre = 0; pre < 3; pre++) {
            for (order = 0; order < 2; order++) {
                memset(&P, 0, sizeof(P));
                dma_pool_state_init(&P, base, DMA_POOL_PAGES);
                h1 = h2 = 0;
                /* 前置きは最初適合の置き場のまま (先頭の近くがまたぐ
                 * 位置なら飛ばされた先) — 番地は決め打ちしない。 */
                if (pre >= 1) { h1 = A(KB(8), 0); CHECK(h1 != 0); }
                if (pre == 2) {
                    h2 = A(KB(8), 0);
                    CHECK(h2 != 0);
                    CHECK(dma_pool_state_free(&P, h1) == 0);
                    h1 = 0;
                }
                rb = order ? NIC_BYTES : PCM_BYTES;
                nb = order ? PCM_BYTES : NIC_BYTES;
                r = A(rb, 4096);
                n = A(nb, 0);
                if (!r || !n)
                    fprintf(stderr, "worst: k=%lu pre=%lu order=%lu\n",
                            (unsigned long)k, (unsigned long)pre,
                            (unsigned long)order);
                CHECK(r != 0 && n != 0);
                CHECK(dma_range_ok(r, rb, DMA_PHYS_LIMIT));
                CHECK(dma_range_ok(n, nb, DMA_PHYS_LIMIT));
                CHECK(!dma_crosses_64k(r, rb) && !dma_crosses_64k(n, nb));
                CHECK(OVERLAP(r, rb, n, nb) == 0);           /* 重ならない */
                CHECK(r >= base && r + rb <= base + KB(64)); /* 池の中 */
                CHECK(n >= base && n + nb <= base + KB(64));
                /* 使用中の前置きと重ならない */
                if (h1) CHECK(OVERLAP(r, rb, h1, KB(8)) == 0 &&
                              OVERLAP(n, nb, h1, KB(8)) == 0);
                if (h2) CHECK(OVERLAP(r, rb, h2, KB(8)) == 0 &&
                              OVERLAP(n, nb, h2, KB(8)) == 0);
            }
        }
    }
}

int main(int argc, char **argv)
{
    const char *c = (argc > 1) ? argv[1] : "";

    if (!strcmp(c, "layout"))             layout();
    else if (!strcmp(c, "sizes"))         sizes();
    else if (!strcmp(c, "skip_boundary")) skip_boundary();
    else if (!strcmp(c, "alignment"))     alignment();
    else if (!strcmp(c, "frees"))         frees();
    else if (!strcmp(c, "leaked"))        leaked();
    else if (!strcmp(c, "exhaust"))       exhaust();
    else if (!strcmp(c, "range_ok"))      range_ok();
    else if (!strcmp(c, "limit"))         limit();
    else if (!strcmp(c, "keep_out"))      keep_out();
    else if (!strcmp(c, "worst"))         worst();
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return failed ? 1 : 0;
}
