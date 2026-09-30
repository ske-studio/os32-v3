/* ======================================================================== */
/*  DMA8237_HOST.C — drivers/dma8237_math.c をそのままホストで回す          */
/*                                                                          */
/*  実物の算数を 1 行も写さずに #include する。dma8237_math.c は I/O を      */
/*  1 つも出さないので、模型は要らない。                                    */
/*                                                                          */
/*  **NP21/W では踏めない分岐**を見る (記録: dma8237_tdd.md):               */
/*    (a) 64KB バンクまたぎ — エミュレータは 8237 の折り返しを模擬しない    */
/*    (b) 16MB 超 — 拡張バンクを持たない前提そのもの                        */
/*    (c) TC 後の FFFFh / 再ロードをまたいだ合成の不採用                    */
/*    (d) バンクレジスタが **等差数列ではない** こと (ch0 だけ 0027h)       */
/* ======================================================================== */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../../drivers/dma8237_math.c"

#define CHECK(x) do { if (!(x)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __func__, __LINE__, #x); failed++; \
} } while (0)

static int failed;

/* 期待値は**数で書く** — 実装と同じ式で作ると両方が同じだけずれても通る。 */
#define BANK_CH0  0x27
#define BANK_CH1  0x21
#define BANK_CH2  0x23
#define BANK_CH3  0x25

/* ------------------------------------------------------------------ */
/*  (d) ポート表 — ch0 のバンクだけ並びが飛ぶ                          */
/* ------------------------------------------------------------------ */
static void port_table(void)
{
    u16 p;
    unsigned int ch;

    CHECK(dma_port_addr(0, &p) == 0 && p == 0x01);
    CHECK(dma_port_count(0, &p) == 0 && p == 0x03);
    CHECK(dma_port_addr(1, &p) == 0 && p == 0x05);
    CHECK(dma_port_count(1, &p) == 0 && p == 0x07);
    CHECK(dma_port_addr(2, &p) == 0 && p == 0x09);
    CHECK(dma_port_count(2, &p) == 0 && p == 0x0B);
    CHECK(dma_port_addr(3, &p) == 0 && p == 0x0D);
    CHECK(dma_port_count(3, &p) == 0 && p == 0x0F);

    CHECK(dma_port_bank(0, &p) == 0 && p == BANK_CH0);
    CHECK(dma_port_bank(1, &p) == 0 && p == BANK_CH1);
    CHECK(dma_port_bank(2, &p) == 0 && p == BANK_CH2);
    CHECK(dma_port_bank(3, &p) == 0 && p == BANK_CH3);

    /* **等差数列ではない。** ch1〜ch3 は 0x21 + 2(ch-1) = 0x1F + 2ch で
     * 並ぶが、**ch0 だけ 0x27 で末尾に飛ぶ** (io_dma.md 152〜155 行)。
     * 式で出すと ch0 が 0x1F (存在しないポート) になる。 */
    CHECK(BANK_CH0 != 0x1F);
    CHECK(BANK_CH0 > BANK_CH3);
    for (ch = 0; ch < DMA_CHAN_COUNT; ch++) {
        u16 naive = (u16)(0x1F + 2 * ch);
        CHECK(dma_port_bank(ch, &p) == 0);
        if (ch == 0) CHECK(p != naive);   /* ここだけ食い違う */
        else         CHECK(p == naive);
    }

    /* 範囲外は表を引かずに負。出力は触らない。 */
    p = 0xDEAD;
    CHECK(dma_port_addr(DMA_CHAN_COUNT, &p) < 0 && p == 0xDEAD);
    CHECK(dma_port_count(99, &p) < 0 && p == 0xDEAD);
    CHECK(dma_port_bank(4, &p) < 0 && p == 0xDEAD);
    CHECK(dma_port_addr(0, (u16 *)0) < 0);
}

/* ------------------------------------------------------------------ */
static void split_addr(void)
{
    u16 a;
    u8 b;

    dma_split_addr(0x0012ABCDUL, &a, &b);
    CHECK(a == 0xABCD);
    CHECK(b == 0x12);

    dma_split_addr(0x002E8000UL, &a, &b);   /* DMA プールの先頭 */
    CHECK(a == 0x8000);
    CHECK(b == 0x2E);

    dma_split_addr(0x00FFFFFFUL, &a, &b);   /* 16MB - 1 */
    CHECK(a == 0xFFFF);
    CHECK(b == 0xFF);

    /* どちらか片方だけでも落ちない。 */
    a = 0; b = 0;
    dma_split_addr(0x00110000UL, &a, (u8 *)0);
    CHECK(a == 0x0000);
    dma_split_addr(0x00110000UL, (u16 *)0, &b);
    CHECK(b == 0x11);
}

/* ------------------------------------------------------------------ */
/*  (a) 64KB またぎ                                                    */
/* ------------------------------------------------------------------ */
static void crosses(void)
{
    /* ちょうど収まる: バンクの先頭から 64KB */
    CHECK(dma_crosses_64k(0x00120000UL, 0x10000UL) == 0);
    /* 1 バイト溢れる */
    CHECK(dma_crosses_64k(0x00120000UL, 0x10001UL) == 1);
    /* バンクの末尾ちょうどで終わる */
    CHECK(dma_crosses_64k(0x0012FFFFUL, 1) == 0);
    /* 1 バイトはみ出す */
    CHECK(dma_crosses_64k(0x0012FFFFUL, 2) == 1);
    /* **プールが跨ぐ境界 0x2F0000 のすぐ手前から 16KB** — 票 §1-3 の例 */
    CHECK(dma_crosses_64k(0x002EF000UL, 0x4000UL) == 1);
    CHECK(dma_crosses_64k(0x002F0000UL, 0x4000UL) == 0);
    /* 0 バイトは「またぐ」= 断る側 (積めないので入口で拾う) */
    CHECK(dma_crosses_64k(0x00120000UL, 0) == 1);
}

/* ------------------------------------------------------------------ */
static void count_bytes(void)
{
    CHECK(dma_count_to_bytes(0) == 1);
    CHECK(dma_count_to_bytes(511) == 512);
    CHECK(dma_count_to_bytes(0xFFFF) == 0x10000UL);
    CHECK(dma_bytes_to_count(1) == 0);
    CHECK(dma_bytes_to_count(512) == 511);
    CHECK(dma_bytes_to_count(0x10000UL) == 0xFFFF);
    /* 往復して戻る */
    CHECK(dma_count_to_bytes(dma_bytes_to_count(1024)) == 1024);
}

/* ------------------------------------------------------------------ */
/*  (c) 安定読みの採用判定                                             */
/* ------------------------------------------------------------------ */
static void accept_pair(void)
{
    u32 limit = 511;   /* 512 バイトを積んだとき */

    /* 一致 */
    CHECK(dma_accept_pair(400, 400, limit) == 1);
    /* 小さく減った (2 回の読みの間に進んだ) */
    CHECK(dma_accept_pair(400, 399, limit) == 1);
    CHECK(dma_accept_pair(400, 400 - DMA_READ_SLACK, limit) == 1);
    /* 減りすぎ = 上位と下位が別の周期のもの */
    CHECK(dma_accept_pair(400, 400 - DMA_READ_SLACK - 1, limit) == 0);
    /* **増えた** = 再ロードをまたいだ。カウントは減る一方。 */
    CHECK(dma_accept_pair(399, 400, limit) == 0);
    /* 設定長を超えた = TC 後の FFFFh か桁借りをまたいだ合成 */
    CHECK(dma_accept_pair(0xFFFF, 0xFFFF, limit) == 0);
    CHECK(dma_accept_pair(limit + 1, limit, limit) == 0);
    CHECK(dma_accept_pair(limit, limit + 1, limit) == 0);
    /* 境界: limit ちょうどは通る (まだ 1 バイトも進んでいない) */
    CHECK(dma_accept_pair(limit, limit, limit) == 1);
    /* 0 で一致 = 残り 1 バイト */
    CHECK(dma_accept_pair(0, 0, limit) == 1);
}

/* ------------------------------------------------------------------ */
/*  (b) 引数の検査 — 検査順も見る                                      */
/* ------------------------------------------------------------------ */
static void check_args(void)
{
    u32 ok_phys = 0x002E8000UL;

    CHECK(dma_check_args(2, ok_phys, 512, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) == 0);
    CHECK(dma_check_args(0, ok_phys, 512, DMA_DIR_FROM_MEM, DMA_MODE_CYCLIC) == 0);
    /* 64KB ちょうどはバンクの先頭からなら通る (プールの 0x2E8000 からは
     * またぐので通らない — それは下の「またぎ」で見る)。 */
    CHECK(dma_check_args(3, 0x002E0000UL, 0x10000UL, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) == 0);
    CHECK(dma_check_args(3, ok_phys, 0x10000UL, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0);

    /* ch は 0〜3 */
    CHECK(dma_check_args(4, ok_phys, 512, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) < 0);
    CHECK(dma_check_args(0xFFFFFFFFUL, ok_phys, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0);
    /* dir / mode は決めた値だけ */
    CHECK(dma_check_args(2, ok_phys, 512, 2, DMA_MODE_SINGLE) < 0);
    CHECK(dma_check_args(2, ok_phys, 512, DMA_DIR_TO_MEM, 2) < 0);
    /* bytes は 1〜65536。**0 は禁止** (カウントに -1 を積むので 65536 になる) */
    CHECK(dma_check_args(2, ok_phys, 0, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) < 0);
    CHECK(dma_check_args(2, ok_phys, 0x10001UL, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0);
    CHECK(dma_check_args(2, ok_phys, 1, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) == 0);
    /* 64KB またぎ */
    CHECK(dma_check_args(2, 0x0012FF00UL, 0x200, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0);
    /* 16MB 以上は**機種によらず**拒否。先頭だけでなく**終端**で見る。 */
    CHECK(dma_check_args(2, 0x01000000UL, 512, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0);
    CHECK(dma_check_args(2, 0x00FFFF00UL, 0x200, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) < 0);
    /* 16MB のすぐ下にぴったり収まるものは通る */
    CHECK(dma_check_args(2, 0x00FFFE00UL, 0x200, DMA_DIR_TO_MEM,
                         DMA_MODE_SINGLE) == 0);
}

/* ------------------------------------------------------------------ */
/*  モード / マスクのバイト                                            */
/* ------------------------------------------------------------------ */
static void mode_bytes(void)
{
    /* 直す前の fdc.c が使っていた値 (ch2)。**数で固定する**。 */
    CHECK(dma_mode_byte(2, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) == 0x46);
    CHECK(dma_mode_byte(2, DMA_DIR_FROM_MEM, DMA_MODE_SINGLE) == 0x4A);
    /* auto-init を足すと bit4 が立つ */
    CHECK(dma_mode_byte(2, DMA_DIR_TO_MEM, DMA_MODE_CYCLIC) == 0x56);
    /* チャネルは下位 2 ビット */
    CHECK(dma_mode_byte(1, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) == 0x45);
    CHECK(dma_mode_byte(3, DMA_DIR_FROM_MEM, DMA_MODE_SINGLE) == 0x4B);
    /* **向きは 8237 の呼び方と逆**: 装置 → メモリ が「ライト転送」。 */
    CHECK(dma_mode_byte(2, DMA_DIR_TO_MEM, DMA_MODE_SINGLE) !=
          dma_mode_byte(2, DMA_DIR_FROM_MEM, DMA_MODE_SINGLE));

    /* マスク: bit2 = MK、bit1-0 = ch (fdc.c の 0x06 / 0x02) */
    CHECK(dma_mask_byte(2, 1) == 0x06);
    CHECK(dma_mask_byte(2, 0) == 0x02);
    CHECK(dma_mask_byte(0, 1) == 0x04);
    CHECK(dma_mask_byte(3, 1) == 0x07);
}

int main(int argc, char **argv)
{
    const char *c = (argc > 1) ? argv[1] : "";

    if (!strcmp(c, "port_table"))      port_table();
    else if (!strcmp(c, "split_addr")) split_addr();
    else if (!strcmp(c, "crosses"))    crosses();
    else if (!strcmp(c, "count_bytes")) count_bytes();
    else if (!strcmp(c, "accept_pair")) accept_pair();
    else if (!strcmp(c, "check_args"))  check_args();
    else if (!strcmp(c, "mode_bytes"))  mode_bytes();
    else { fprintf(stderr, "unknown case: %s\n", c); return 2; }
    return failed ? 1 : 0;
}
