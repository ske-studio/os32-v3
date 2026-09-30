/* ======================================================================== */
/*  PC98.H — PC-9801/9821 ハードウェア共通定数                               */
/*                                                                          */
/*  複数モジュールから参照されるPC-98固有のI/Oポート・コマンド・メモリ        */
/*  アドレス等を一元管理する。                                               */
/*                                                                          */
/*  出典: PC9800Bible §1-3, §2-2, §2-3, §2-6, §2-7, §4-2, §4-3            */
/* ======================================================================== */

#ifndef PC98_H
#define PC98_H

/* ====================================================================== */
/*  I/Oウェイト                                                            */
/* ====================================================================== */
/* ポート0x5Fへのダミーアクセスで約0.6µsの遅延を挿入する。                  */
/* PC-98のI/Oバスウェイトに使用される標準的手法。(PC9800Bible §4-4)          */
#define PC98_IO_WAIT_PORT   0x5F


/* ====================================================================== */
/*  PIC (8259相当) — PC9800Bible §1-4, §4-3 #1/#2                         */
/* ====================================================================== */
/*  マスタ: IRQ0-7, スレーブ: IRQ8-15 (カスケード IRQ7)                     */
/*  ポート定数は kernel/idt.h (PIC1_CMD, PIC1_DATA, PIC2_xxx, OCW2_EOI)     */
/*  が正。                                                                  */
/*  かつてはここに別名の重複定義があり 3 か所 (idt.h/pc98.h/isr_stub.asm)   */
/*  で食い違うリスクがあった。C 側は idt.h を使うこと。                     */


/* ====================================================================== */
/*  システムポート 8255相当 — PC9800Bible §2-2, §4-3 #7                    */
/* ====================================================================== */
/*  ポートA (31H): DIPSW2読み出し (リードのみ)                              */
/*  ポートB (33H): モデム信号・割り込み状態読み出し                          */
/*  ポートC (35H): 一括リード/ライト                                        */
/*  ポートC BSR (37H): 個別ビットセットリセット                             */

#define SYSPORT_A           0x31    /* ポートA: DIPSW2 (リード) */
#define SYSPORT_B           0x33    /* ポートB: 信号状態 (リード) */
#define SYSPORT_C           0x35    /* ポートC: 一括 (リード/ライト) */
#define SYSPORT_C_BSR       0x37    /* ポートC BSR: 個別制御 (ライト) */

/* PortB (33H) ビット構成 */
#define SYSPORT_B_CI        0x80    /* RS-232C CI信号 (負論理) */
#define SYSPORT_B_CS        0x40    /* RS-232C CS/CTS信号 (負論理) */
#define SYSPORT_B_CD        0x20    /* RS-232C CD信号 (負論理) */
#define SYSPORT_B_INT3      0x10    /* HDD割り込み発生中 */
#define SYSPORT_B_CRTT      0x08    /* CRTタイプ (0:15KHz 1:24KHz) */

/* PortC BSR (37H) 出力値 — 表2-3
 *
 * **0035h (ポート C) は丸ごと書かない。** RS-232C の割り込み許可 (bit0-2) と
 * 同じバイトに BUZ (bit3)・MCHKEN (bit4)・SHUT1 (bit5)・PSTBM (bit6)・
 * SHUT0 (bit7) が同居していて、全体書きはこれらを巻き添えにする
 * (実機 PC-9821Ra266 で rshell 中にビープが鳴り続けた、2026-09-24。
 * docs/POLICY_DEBUG.md §4-59)。1 ビットずつ 0037h のこのコマンドで操作する。
 *
 * **BUZ の極性は資料どうしで食い違う。UNDOCUMENTED を採る**
 * (docs/hw/undocumented/io_syste.md の I/O 0035h bit3 と I/O 0037h:
 * bit3 = 1 で停止、06h = 鳴動、07h = 停止)。PC9800Bible §2-2 表2-3 は逆に
 * 書いているが、NP21/W (sound/beepc.c `buz = (sysport.c & 8)?0:1`) も
 * UNDOCUMENTED と同じ向き。2026-09-24 まではここが Bible の向き
 * (BUZ_OFF = 06h) で、buz_off() が実機でブザーを鳴らしていた。
 * ほかの行は UNDOCUMENTED と Bible で一致する (偶数 = 0、奇数 = 1)。 */
#define BSR_RXRE_OFF        0x00    /* RS-232C 受信割り込み禁止 */
#define BSR_RXRE_ON         0x01    /* RS-232C 受信割り込み許可 */
#define BSR_TXEE_OFF        0x02    /* RS-232C TXEMPTY割り込み禁止 */
#define BSR_TXEE_ON         0x03    /* RS-232C TXEMPTY割り込み許可 */
#define BSR_TXRE_OFF        0x04    /* RS-232C 送信割り込み禁止 */
#define BSR_TXRE_ON         0x05    /* RS-232C 送信割り込み許可 */
#define BSR_BUZ_ON          0x06    /* ブザー鳴動 (bit3 = 0) — UNDOCUMENTED */
#define BSR_BUZ_OFF         0x07    /* ブザー停止 (bit3 = 1) — UNDOCUMENTED */
#define BSR_MCKEN_OFF       0x08    /* RAM パリティチェック無効 (MCHKEN = 0) */
#define BSR_MCKEN_ON        0x09    /* RAM パリティチェック有効 (MCHKEN = 1) */
#define BSR_SHUT1_CLR       0x0A    /* SHUT1 = 0 */
#define BSR_SHUT1_SET       0x0B    /* SHUT1 = 1 */
#define BSR_PSTBM_OFF       0x0C    /* PSTB マスクなし */
#define BSR_PSTBM_ON        0x0D    /* PSTB マスク */
#define BSR_SHUT0_CLR       0x0E    /* SHUT0 = 0 */
#define BSR_SHUT0_SET       0x0F    /* SHUT0 = 1 */


/* ====================================================================== */
/*  CPU制御 — PC9800Bible §4-3 #31                                        */
/* ====================================================================== */
#define CPU_RESET_PORT      0xF0    /* CPUリセット (ライトでリセット) */
#define CPU_A20_PORT        0xF2    /* A20ゲート制御 */

/* x86 制御レジスタのビット (CR0_PE / CR0_PG) は CPU の持ち物なので
 * arch/x86/arch_cpu.h へ移した (移植準備 順序 5)。 */


/* ====================================================================== */
/*  NMI制御 — PC9800Bible §4-3 #10                                        */
/* ====================================================================== */
#define NMI_RESET           0x50    /* NMIリセット (禁止) */
#define NMI_SET             0x52    /* NMIセット (許可) */


/* ====================================================================== */
/*  PIT (8253相当) — PC9800Bible §2-3, §4-3 #13                           */
/* ====================================================================== */
/*  カウンタ#0: インターバルタイマ (IRQ0)                                   */
/*  カウンタ#1: スピーカー (ビープ)                                         */
/*  カウンタ#2: RS-232C通信速度                                            */
#define PIT_CNT0            0x71    /* カウンタ#0 データ */
#define PIT_CNT1_BEEP       0x3FDBUL /* カウンタ#1 データ (ビープ) */
#define PIT_CNT2            0x75    /* カウンタ#2 データ (RS-232C) */
#define PIT_MODE            0x77    /* モードレジスタ */
#define PIT_BEEP_MODE       0x3FDFUL /* ビープ用モードレジスタ */

/* PIT モードレジスタ ビット構成 */
/* D7-D6: カウンタ選択 (SC) */
#define PIT_SC_CNT0         0x00    /* カウンタ#0 */
#define PIT_SC_CNT1         0x40    /* カウンタ#1 */
#define PIT_SC_CNT2         0x80    /* カウンタ#2 */
/* D5-D4: 読み書き指定 (RL) */
#define PIT_RL_LATCH        0x00    /* ラッチコマンド */
#define PIT_RL_LSB          0x10    /* LSBのみ */
#define PIT_RL_MSB          0x20    /* MSBのみ */
#define PIT_RL_LSBMSB       0x30    /* LSB→MSBの順 */
/* D3-D1: モード選択 (M) */
#define PIT_M_INTONTC       0x00    /* モード0: カウント終了割り込み */
#define PIT_M_RATEGEN       0x04    /* モード2: レートジェネレータ */
#define PIT_M_SQWAVE        0x06    /* モード3: 方形波 */
#define PIT_M_SWTRIG        0x08    /* モード4: ソフトウェアトリガ */
/* D0: カウント形式 */
#define PIT_BIN             0x00    /* バイナリ */
#define PIT_BCD             0x01    /* BCD */

/* システムクロック定数 (Hz)                                                */
/* PC9800BibleとUNDOCUMENTED io_tcu.mdでMHz系とクロック値の対応が逆転している */
/* 安全のためMHz系不依存の名前で定義。実行時にポート42h bit5等で判別推奨 */
#define SYSCLK_1997         1996800UL   /* 1.9968MHz (NP21/Wデフォルト) */
#define SYSCLK_2458         2457600UL   /* 2.4576MHz */

/* どちらのクロックかは BIOS ワークエリア 0000:0501h の bit7 で分かる。
 * **定義はここ 1 か所** ([C4]) — 読むのは kernel/sysclk.c だけで、
 * drivers/serial.h は別名 (TIMER_CLK_*) を張るだけにしてある。
 * 根拠の資料と食い違いの決着は drivers/serial.h の注記を参照。 */
#define BIOS_WORK_SYSCLK    0x00000501UL  /* BYTE: bit7=1 なら 8MHz系 */
#define BIOS_SYSCLK_8MHZ    0x80


/* ====================================================================== */
/*  テキストGDC (µPD7220, テキスト用) — PC9800Bible §2-6-2, §4-3 #11      */
/* ====================================================================== */
#define GDC_TEXT_STAT        0x60    /* ステータスリード */
#define GDC_TEXT_PARAM       0x60    /* パラメータライト */
#define GDC_TEXT_DATA        0x62    /* データリード */
#define GDC_TEXT_CMD         0x62    /* コマンドライト */
#define GDC_TEXT_CRTIRQ      0x64    /* CRT割り込みリセット */

/* テキストGDCステータスビット (60Hリード) */
#define GDC_STAT_DRDY        0x01   /* データレディ */
#define GDC_STAT_FFUL        0x02   /* FIFOフル */
#define GDC_STAT_FEMP        0x04   /* FIFOエンプティ */
#define GDC_STAT_DRAW        0x08   /* 描画中 */
#define GDC_STAT_DMA         0x10   /* DMA実行中 */
#define GDC_STAT_VSYNC       0x20   /* 垂直同期中 */
#define GDC_STAT_HBLANK      0x40   /* 水平ブランク中 */
#define GDC_STAT_LPEN        0x80   /* ライトペン検出 */

/* テキストGDCコマンド (62Hライト) — 表2-26 */
#define GDC_CMD_RESET        0x00   /* リセット */
#define GDC_CMD_SYNC_DE      0x0F   /* SYNC (表示許可DE=1) */
#define GDC_CMD_SYNC         0x0E   /* SYNC (表示禁止DE=0) */
#define GDC_CMD_MASTER       0x6F   /* マスタモード設定 */
#define GDC_CMD_SLAVE        0x6E   /* スレーブモード設定 */
#define GDC_CMD_START        0x0D   /* 表示開始 (START) */
#define GDC_CMD_STOP         0x0C   /* 表示停止 (STOP) */
#define GDC_CMD_ZOOM         0x46   /* ズーム設定 */
#define GDC_CMD_SCROLL       0x70   /* スクロール設定 */
#define GDC_CMD_CSRFORM      0x4B   /* カーソル形状設定 */
#define GDC_CMD_PITCH        0x47   /* メモリ幅設定 */
#define GDC_CMD_CSRW         0x49   /* カーソル位置設定 (CSRW) */
#define GDC_CMD_CSRR         0xE0   /* カーソル位置読出し (CSRR) */
/* 旧 GDC_CMD_CSON (0x0B) は uPD7220 に存在しないコマンドで、カーソルは
 * 出なかった。表示 ON/OFF は CSRFORM (0x4B) P1 の DC ビットで行う。 */
#define GDC_CSRFORM_DC       0x80   /* CSRFORM P1 bit7: カーソル表示 */
#define GDC_TEXT_LINES_PER_ROW 16   /* テキスト 1 行のラスタ数 (CSRFORM P1 = LR-1) */
#define GDC_TEXT_CSR_TOP     14     /* カーソル上端ラスタ (2 ライン下線) */
#define GDC_TEXT_CSR_BOTTOM  15     /* カーソル下端ラスタ */
#define GDC_TEXT_CSR_BR_HI   0x03   /* 点滅周期 上位 3bit (BIOS 既定値と同じ) */
#define GDC_CMD_WRITE        0x20   /* メモリ書込み準備 */
#define GDC_CMD_READ         0xA0   /* メモリ読出し指示 */


/* ====================================================================== */
/*  CRTC — PC9800Bible §2-6-4, §4-3 #12                                   */
/* ====================================================================== */
#define CRTC_CHRLINE         0x70   /* キャラクタ位置ライン数 */
#define CRTC_BODYLINE        0x72   /* ボディフェイスライン数 */
#define CRTC_CHARLINE        0x74   /* キャラクタライン数 */
#define CRTC_SCROLL          0x76   /* スムーススクロールライン数 */
#define CRTC_SCROLL_TOP      0x78   /* スクロール上辺位置行数 */
#define CRTC_SCROLL_LINES    0x7A   /* スクロール行数 */


/* ====================================================================== */
/*  モードフリップフロップ — PC9800Bible §2-6-2 表2-23, §4-3 #11          */
/* ====================================================================== */

/* モードFF1 (68H) */
#define MODE_FF1_PORT        0x68   /* モードFF1ポート */
#define MFF1_ATR_VLINE       0x00   /* ATR4=バーチカルライン */
#define MFF1_ATR_GRAPH       0x01   /* ATR4=簡易グラフ */
#define MFF1_COLOR_GFX       0x02   /* カラーグラフィックモード */
#define MFF1_MONO_GFX        0x03   /* モノクログラフィックモード */
#define MFF1_TEXT_80         0x04   /* テキスト80字モード */
#define MFF1_TEXT_40         0x05   /* テキスト40字モード */
#define MFF1_ANK_6x8         0x06   /* ANK 6×8ドット */
#define MFF1_ANK_7x13        0x07   /* ANK 7×13ドット */
#define MFF1_HIRES           0x08   /* 高解像度モード (400ライン) */
#define MFF1_200LINE         0x09   /* 縦200ラインモード */
#define MFF1_KCG_CODE        0x0A   /* KCGコードアクセスモード */
#define MFF1_KCG_BITMAP      0x0B   /* KCGビットマップモード */
#define MFF1_NVRAM_LOCK      0x0C   /* 不揮発メモリ書込み不可 */
#define MFF1_NVRAM_UNLOCK    0x0D   /* 不揮発メモリ書込み可 */
#define MFF1_DISP_OFF        0x0E   /* 画面表示不可 */
#define MFF1_DISP_ON         0x0F   /* 画面表示可 */

/* モードFF2 (6AH) */
#define MODE_FF2_PORT        0x6A   /* モードFF2ポート */
#define MFF2_8COLOR          0x00   /* 8色グラフィックモード */
#define MFF2_16COLOR         0x01   /* 16色グラフィックモード */
#define MFF2_GRCG_MODE       0x04   /* GRCG互換モード */
#define MFF2_EGC_MODE        0x05   /* EGC拡張モード */

/* ボーダーカラー (6CH) */
#define BORDER_COLOR_PORT    0x6C


/* ====================================================================== */
/*  グラフィックGDC (µPD7220, グラフィック用) — PC9800Bible §2-7, §4-3 #19 */
/* ====================================================================== */
#define GDC_GFX_STAT         0xA0   /* ステータスリード */
#define GDC_GFX_PARAM        0xA0   /* パラメータライト */
#define GDC_GFX_DATA         0xA2   /* データリード */
#define GDC_GFX_CMD          0xA2   /* コマンドライト */
#define GDC_DISP_PAGE        0xA4   /* 表示ページ設定 (DP) */
#define GDC_ACCESS_PAGE      0xA6   /* 描画ページ設定 (WP) */

/* グラフィックGDC 400ラインモード設定値 */
#define GDC_GFX_400LINE      0x4B   /* SYNC P3: 400ラインモード */


/* ====================================================================== */
/*  テキストVRAM — PC9800Bible §2-6-1, §4-2                               */
/* ====================================================================== */
/* 基本アドレス・サイズは tvram.h にも定義あり (TVRAM_BASE, TVRAM_ATTR等)      */
#define TVRAM_CHAR_BASE      0x000A0000UL  /* 文字エリア先頭 */
#define TVRAM_ATTR_BASE      0x000A2000UL  /* アトリビュートエリア先頭 */
#define TVRAM_CG_WINDOW      0x000A4000UL  /* CGウィンドウ先頭 */
#define TVRAM_END            0x000A5000UL  /* テキストVRAM末端 */

#define TVRAM_COLS           80      /* 横桁数 */
#define TVRAM_ROWS           25      /* 縦行数 (標準) */
#define TVRAM_ROWS_30        30      /* 縦行数 (30行モード) */
#define TVRAM_BPR            160     /* Bytes Per Row (80桁×2バイト) */

/* テキストアトリビュートビット — PC9800Bible §2-6-1 図2-25 */
#define TATTR_SECRET         0x00   /* D0: シークレット (0=表示) */
#define TATTR_VISIBLE        0x01   /* D0: 表示 */
#define TATTR_BLINK          0x02   /* D1: ブリンク (点滅) */
#define TATTR_REVERSE        0x04   /* D2: リバース (反転) */
#define TATTR_UNDERLINE      0x08   /* D3: アンダーライン */
#define TATTR_VLINE          0x10   /* D4: バーチカルライン/簡易グラフ */
#define TATTR_B_BLUE         0x20   /* D5: 青 */
#define TATTR_B_RED          0x40   /* D6: 赤 */
#define TATTR_B_GREEN        0x80   /* D7: 緑 */

/* よく使う色+属性の組み合わせ */
#define TATTR_WHITE          0xE1   /* 白 (GRB=111) + 表示 */
#define TATTR_CYAN           0xA1   /* 水色 (GB=11) + 表示 */
#define TATTR_YELLOW         0xC1   /* 黄色 (GR=11) + 表示 */
#define TATTR_GREEN          0x81   /* 緑 (G=1) + 表示 */
#define TATTR_RED            0x41   /* 赤 (R=1) + 表示 */
#define TATTR_GRAY           0x01   /* 灰色 (表示のみ、色は白またはグレーとして扱われる) */
#define TATTR_BLACK          0x00   /* 黒 (非表示/シークレット) */


/* ====================================================================== */
/*  グラフィックVRAM — PC9800Bible §2-7, §4-2                             */
/* ====================================================================== */
/* 各プレーンのベースアドレスは gfx.h にも定義あり (VRAM_PLANE_B等)          */
#define GVRAM_PLANE_B        0x000A8000UL  /* プレーン0: 青 */
#define GVRAM_PLANE_R        0x000B0000UL  /* プレーン1: 赤 */
#define GVRAM_PLANE_G        0x000B8000UL  /* プレーン2: 緑 */
#define GVRAM_PLANE_I        0x000E0000UL  /* プレーン3: 輝度 */

#define GFX_SCREEN_W         640     /* 画面幅 (ピクセル) */
#define GFX_SCREEN_H         400     /* 画面高さ (400ラインモード) */
#define GFX_SCREEN_H_200     200     /* 画面高さ (200ラインモード) */


/* ====================================================================== */
/*  BIOS デバイス (INT 1B/13) — PC9800Bible                                 */
/* ====================================================================== */
#define BOOT_DRIVE_FDD        0x90    /* 1MB FDD (PC-98 BIOS標準) */
#define BOOT_DRIVE_FDD_144    0x30    /* 1.44MB FDD */
#define BOOT_DRIVE_HDD        0x80    /* SASI/IDE HDD基本 */
#define BOOT_DRIVE_HDD_IDE    0xA0    /* IDE HDD */


/* ====================================================================== */
/*  BIOS ROM — PC9800Bible §4-2                                           */
/* ====================================================================== */
/* BIOS ワークエリア: メモリ容量 (正典 docs/hw/undocumented/memsys.md)。
 *   0401h  BYTE  100000h〜FFFFFFh の使用可能プロテクトモードメモリ、128KB 単位。
 *                最大 70h (= 14MB)。「16MB システム空間を使用しない」設定の
 *                ときだけ 78h (= 15MB) になる。
 *   0594h  WORD  1000000h 以降の使用可能プロテクトモードメモリ、MB 単位。
 *                PC-H98 / PC-9821Af 以降 / PC-9801BA2・BS2・BX2・BA3・BX3・BX4。
 *                16MB 超対応の HIMEM.SYS を組むと 0 に潰されるが、OS32 は
 *                DOS を通さずに起動するので影響を受けない。 */
#define BIOS_WORK_MEM_128KB  0x00000401UL  /* BYTE: 15MB までの容量 */
#define BIOS_WORK_MEM_HIGH_MB 0x00000594UL /* WORD: 16MB 超の容量 (MB) */

/* BIOS ワークエリア: キーボード (正典 docs/hw/undocumented/memsys.md、Bible §2-5 表2-16)。
 *   053Ah  BYTE  KB_SHFT_STS シフトキー押下状態フラグ。INT 09h ハンドラが make で
 *                セット、break でリセットする (「1= 押されている」)。INT 18h AH=02h の値。
 *                bit4 CTRL / bit3 GRPH / bit2 カナ / bit1 CAPS / bit0 SHIFT。
 *                読むのは drivers/kbd.c の kbd_init (paging_init より前) だけで、
 *                カナ・CAPS のロックの初期値にする (TASK_KBD_NAV §2)。 */
#define BIOS_WORK_KB_SHIFT   0x0000053AUL  /* BYTE: シフトキー押下状態 */
#define BIOS_KB_SHIFT_CAPS   0x02
#define BIOS_KB_SHIFT_KANA   0x04

#define BIOS_ROM_BASE        0x000E8000UL  /* BIOS ROM先頭 */
#define BIOS_ROM_END         0x00100000UL  /* BIOS ROM末端 (1MB) */


/* ====================================================================== */
/*  プリンタ (8255相当) — PC9800Bible §4-3 #8                              */
/* ====================================================================== */
#define PRT_DATA             0x40    /* プリンタデータ出力 */
#define PRT_STATUS           0x42    /* プリンタステータス */
#define PRT_CTRL             0x44    /* プリンタ制御 */
#define PRT_MODE             0x46    /* 8255モード/PortC個別 */

/* ステータスビット (42Hリード) */
#define PRT_STAT_MOD         0x20   /* D5: システムクロック (0:10MHz 1:8MHz) */
#define PRT_STAT_BSY         0x04   /* D2: BUSY (0=ビジー) */


/* ====================================================================== */
/*  マウス (8255相当) — PC9800Bible §2-11, §4-3 #28                       */
/* ====================================================================== */
#define MOUSE_DATA           0x7FD9UL  /* ポートA: マウスデータ */
#define MOUSE_DIPSW          0x7FDBUL  /* ポートB: クロック/DIPSW */
#define MOUSE_CTRL           0x7FDDUL  /* ポートC: 制御 */
#define MOUSE_MODE           0x7FDFUL  /* 8255モード/PortC個別 */
#define MOUSE_INTERVAL       0xBFDBUL  /* 割り込み間隔設定 */

/* マウスデータ ビット (7FD9Hリード) — UNDOCUMENTED io_mouse.md 準拠       */
/* 注意: PC9800Bibleは D6=RIGHT としているが、UNDOCUMENTEDの実機解析では  */
/*       D6=MIDDLE(中ボタン), D5=RIGHT（右ボタン）が正しい                  */
#define MOUSE_LEFT           0x80   /* D7: 左ボタン (0=押下) */
#define MOUSE_MIDDLE         0x40   /* D6: 中ボタン (0=押下) Undocumented */
#define MOUSE_RIGHT          0x20   /* D5: 右ボタン (0=押下) */


#endif /* PC98_H */
