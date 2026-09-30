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
    last_rc = dma_pool_state_alloc(&P, bytes, align, &out);
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
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return failed ? 1 : 0;
}
