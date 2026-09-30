/* ======================================================================== */
/*  NE2000_REGS.H — DP8390 (NE2000 互換) レジスタ定数の正典                  */
/*                                                                          */
/*  出典: National Semiconductor DP8390D/NS32490D データシート §10。          */
/*  値はカードの BASE からのオフセット (8bit レジスタ, BASE+0x00〜0x0F)。     */
/*  カード固有のポート (データポート・リセットポート) はここに置かない        */
/*  (LGY-98 は drivers/lgy98.h)。PC/AT の NE2000 (+0x10 データ, +0x1F reset)  */
/*  の定数を流用しないこと。                                                  */
/*                                                                          */
/*  ne2000_io.asm はポート番号を引数で受け取るので、この表を NASM 側に        */
/*  複製・生成する必要はない (計画 §3 の「生成 include」は不要と判断)。       */
/* ======================================================================== */

#ifndef NE2000_REGS_H
#define NE2000_REGS_H

/* ---- Command Register (全ページ共通, offset 0) ---- */
#define NE2K_CR             0x00
#define NE2K_CR_STP         0x01    /* STOP: 送受信停止 (リセット状態) */
#define NE2K_CR_STA         0x02    /* START */
#define NE2K_CR_TXP         0x04    /* 送信開始 (完了で自動クリア) */
#define NE2K_CR_RD0         0x08    /* Remote DMA: read */
#define NE2K_CR_RD1         0x10    /* Remote DMA: write */
#define NE2K_CR_RD2         0x20    /* Remote DMA: abort / complete */
#define NE2K_CR_SEND_PKT    (NE2K_CR_RD0 | NE2K_CR_RD1) /* send packet cmd (未使用) */
#define NE2K_CR_PS0         0x40
#define NE2K_CR_PS1         0x80
#define NE2K_CR_PAGE0       0x00
#define NE2K_CR_PAGE1       NE2K_CR_PS0
#define NE2K_CR_PAGE2       NE2K_CR_PS1
#define NE2K_CR_PAGE_MASK   (NE2K_CR_PS0 | NE2K_CR_PS1)
#define NE2K_CR_RD_MASK     (NE2K_CR_RD0 | NE2K_CR_RD1 | NE2K_CR_RD2)

/* ---- Page 0 (W = 書き込み時, R = 読み出し時) ---- */
#define NE2K_P0_PSTART      0x01    /* W: 受信リング開始ページ    R: CLDA0 */
#define NE2K_P0_PSTOP       0x02    /* W: 受信リング終端 (排他的) R: CLDA1 */
#define NE2K_P0_BNRY        0x03    /* R/W: 境界ページ */
#define NE2K_P0_TPSR        0x04    /* W: 送信開始ページ          R: TSR */
#define NE2K_P0_TSR         0x04
#define NE2K_P0_TBCR0       0x05    /* W: 送信バイト数 (low)      R: NCR */
#define NE2K_P0_TBCR1       0x06    /* W: 送信バイト数 (high)     R: FIFO */
#define NE2K_P0_ISR         0x07    /* R/W: 割り込み状態 (1 を書いてクリア) */
#define NE2K_P0_RSAR0       0x08    /* W: Remote DMA 開始アドレス (low)  R: CRDA0 */
#define NE2K_P0_RSAR1       0x09    /* W: Remote DMA 開始アドレス (high) R: CRDA1 */
#define NE2K_P0_RBCR0       0x0A    /* W: Remote DMA バイト数 (low) */
#define NE2K_P0_RBCR1       0x0B    /* W: Remote DMA バイト数 (high) */
#define NE2K_P0_RCR         0x0C    /* W: 受信設定                R: RSR */
#define NE2K_P0_RSR         0x0C
#define NE2K_P0_TCR         0x0D    /* W: 送信設定                R: CNTR0 (FAE) */
#define NE2K_P0_CNTR0       0x0D
#define NE2K_P0_DCR         0x0E    /* W: データ設定              R: CNTR1 (CRC) */
#define NE2K_P0_CNTR1       0x0E
#define NE2K_P0_IMR         0x0F    /* W: 割り込みマスク          R: CNTR2 (missed) */
#define NE2K_P0_CNTR2       0x0F

/* ---- Page 1 ---- */
#define NE2K_P1_PAR0        0x01    /* 物理アドレス PAR0..PAR5 (0x01..0x06) */
#define NE2K_P1_CURR        0x07    /* 受信の現在ページ (NIC が書く) */
#define NE2K_P1_MAR0        0x08    /* マルチキャスト表 MAR0..MAR7 (0x08..0x0F) */

/* ---- Page 2 (診断用, 読み出しのみ使う) ---- */
#define NE2K_P2_PSTART      0x01
#define NE2K_P2_PSTOP       0x02
#define NE2K_P2_TPSR        0x04
#define NE2K_P2_RCR         0x0C
#define NE2K_P2_TCR         0x0D
#define NE2K_P2_DCR         0x0E
#define NE2K_P2_IMR         0x0F

/* ---- ISR / IMR ビット (IMR は同じ位置, RST はマスク不可) ---- */
#define NE2K_ISR_PRX        0x01    /* 受信完了 (正常) */
#define NE2K_ISR_PTX        0x02    /* 送信完了 (正常) */
#define NE2K_ISR_RXE        0x04    /* 受信エラー */
#define NE2K_ISR_TXE        0x08    /* 送信エラー */
#define NE2K_ISR_OVW        0x10    /* 受信リング上書き (overwrite warning) */
#define NE2K_ISR_CNT        0x20    /* カウンタ MSB セット */
#define NE2K_ISR_RDC        0x40    /* Remote DMA 完了 */
#define NE2K_ISR_RST        0x80    /* リセット状態 */
#define NE2K_ISR_ALL        0xFF

/* ---- DCR ---- */
#define NE2K_DCR_WTS        0x01    /* word transfer select (16bit) */
#define NE2K_DCR_BOS        0x02    /* byte order select */
#define NE2K_DCR_LAS        0x04    /* long address select (32bit DMA, 未使用) */
#define NE2K_DCR_LS         0x08    /* loopback select: 1 = 通常動作 */
#define NE2K_DCR_ARM        0x10    /* auto-initialize remote (send packet, 未使用) */
#define NE2K_DCR_FT0        0x20    /* FIFO threshold */
#define NE2K_DCR_FT1        0x40
/* 16bit PIO, 通常動作, FIFO 8 word (NE2000 系ドライバの標準値 0x49) */
#define NE2K_DCR_DEFAULT    (NE2K_DCR_WTS | NE2K_DCR_LS | NE2K_DCR_FT1)

/* ---- RCR ---- */
#define NE2K_RCR_SEP        0x01    /* save errored packets */
#define NE2K_RCR_AR         0x02    /* accept runt (<64B) */
#define NE2K_RCR_AB         0x04    /* accept broadcast */
#define NE2K_RCR_AM         0x08    /* accept multicast (MAR 経由) */
#define NE2K_RCR_PRO        0x10    /* promiscuous */
#define NE2K_RCR_MON        0x20    /* monitor: 受信をバッファに入れない */

/* ---- TCR ---- */
#define NE2K_TCR_CRC        0x01    /* 1 = CRC を付けない (inhibit) */
#define NE2K_TCR_LB0        0x02    /* loopback mode 1: 内部 (NIC 内) */
#define NE2K_TCR_LB1        0x04    /* loopback mode 2/3: ENDEC / 外部 */
#define NE2K_TCR_ATD        0x08
#define NE2K_TCR_OFST       0x10
#define NE2K_TCR_NORMAL     0x00
#define NE2K_TCR_LOOP_INT   NE2K_TCR_LB0

/* ---- TSR ---- */
#define NE2K_TSR_PTX        0x01    /* 送信成功 */
#define NE2K_TSR_COL        0x04    /* 衝突あり (成功) */
#define NE2K_TSR_ABT        0x08    /* 16 回衝突で中止 */
#define NE2K_TSR_CRS        0x10    /* キャリア喪失 */
#define NE2K_TSR_FU         0x20    /* FIFO underrun */
#define NE2K_TSR_CDH        0x40    /* heartbeat 喪失 */
#define NE2K_TSR_OWC        0x80    /* out-of-window collision */

/* ---- RSR (受信ヘッダ 1 バイト目と同じ) ---- */
#define NE2K_RSR_PRX        0x01    /* 正常受信 */
#define NE2K_RSR_CRC        0x02
#define NE2K_RSR_FAE        0x04
#define NE2K_RSR_FO         0x08    /* FIFO overrun */
#define NE2K_RSR_MPA        0x10    /* missed packet */
#define NE2K_RSR_PHY        0x20    /* 0 = 物理宛, 1 = broadcast/multicast */
#define NE2K_RSR_DIS        0x40    /* receiver disabled (monitor) */
#define NE2K_RSR_DFR        0x80    /* deferring */

/* ---- NIC RAM の幾何 ---- */
#define NE2K_PAGE_SHIFT     8
#define NE2K_PAGE_SIZE      256
#define NE2K_RX_HDR_LEN     4       /* status, next, count(lo), count(hi) */
#define NE2K_PROM_LEN       32      /* PROM 16 バイト × 2 (16bit カードは各バイトが重複) */
#define NE2K_PROM_SIG       0x57    /* PROM byte 14/15 = 'W' (NE2000 署名) */

/* ---- Ethernet フレーム ---- */
#define NE2K_ETH_HDR_LEN    14
#define NE2K_ETH_MIN_LEN    60      /* FCS を除く最小長 (padding 後) */
#define NE2K_ETH_MAX_LEN    1514    /* FCS を除く最大長 (非 VLAN) */
#define NE2K_ETH_FCS_LEN    4
#define NE2K_ETH_ADDR_LEN   6

#endif /* NE2000_REGS_H */
