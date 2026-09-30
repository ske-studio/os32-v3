/* ======================================================================== */
/*  PEGC.H — PC-9821 拡張グラフアーキテクチャ (PEGC) のポート/MMIO 定数      */
/*                                                                          */
/*  256 色パックドピクセル表示 (640x480 / 640x400) を扱うための番地はすべて  */
/*  このファイルに集める ([C4] 三層定数 / 票 H2 の鉄則)。gfx_core.c と GUI   */
/*  側には 9821 の文字を一切書かない — バックエンド gfx/backend_pegc.c だけ  */
/*  がこのヘッダを読む。                                                     */
/*                                                                          */
/*  出典 (docs/hw/ = ローカルミラー。著作物なので git 管理外):                */
/*    [B]  docs/hw/PC9800Bible/3-2_グラフィック256色表示.md                   */
/*         §3-2-1 (6Ah / 09A0h / 09A8h / パレット), §3-2-3 表3-4 (E0000h      */
/*         メモリマップト I/O), アドレス式 ADR = y*640 + x                    */
/*    [U]  docs/hw/undocumented/io_disp.md    (006Ah / 09A0h / 09A8h /       */
/*                                             パレット / 00A4h・00A6h)      */
/*    [UM] docs/hw/undocumented/io_mem.md     (043Bh 16MB 空間メモリ制御)     */
/*    [US] docs/hw/undocumented/memsys.md     (0000:045Ch / 0597h / 0459h)   */
/*    [BM] docs/hw/PC9800Bible/4-2_メモリマップ.md (F00000h 周辺のマップ)     */
/*  両者が食い違うときは UNDOCUMENTED を優先する (docs/INDEX.md)。           */
/* ======================================================================== */

#ifndef __PEGC_H
#define __PEGC_H

#include "types.h"
#include "pc98.h"    /* MODE_FF2_PORT (0x6A) — モード F/F2 は 9801 と共通 */

/* ------------------------------------------------------------------------ */
/*  1. モード F/F2 (I/O 6Ah) — 拡張グラフィックモードへの出入り              */
/*                                                                          */
/*  出典 [B] §3-2-1 表3-2 / [U] I/O 006Ah。                                  */
/*  07h は「モード変更の解錠」であって 256 色化そのものではない。20h/21h は   */
/*  解錠中だけ有効で、06h で再施錠する ([B] の MATE サンプルは設定後に 06h)。 */
/*  ポート番号は pc98.h の MODE_FF2_PORT (0x6A) を使う (定数の二重定義回避)。 */
/* ------------------------------------------------------------------------ */
#define PEGC_FF2_UNLOCK        0x07  /* 拡張モード変更可 (解錠) */
#define PEGC_FF2_LOCK          0x06  /* 拡張モード変更不可 (施錠) */
#define PEGC_FF2_STD_GFX       0x20  /* 標準グラフィックモード (16 色) */
#define PEGC_FF2_EXT_GFX       0x21  /* 拡張グラフィックモード (256 色) */

/* VRAM 構成 ([U] I/O 006Ah, Undocumented, PC-H98 / PC-9821)。
 * 400 ライン構成は 00A4h (表示ページ選択) が使えるが 128/256KB バウンダリ。
 * 800 ライン構成は「480 ラインモードのデフォルト」で 256/512KB バウンダリ、
 * かわりに 00A4h が使えない = 480 ラインでハードウェアページ切替は無い。 */
#define PEGC_FF2_VRAM_400L     0x68  /* 400 ライン構成 (00A4h 可) */
#define PEGC_FF2_VRAM_800L     0x69  /* 800 ライン構成 (480 ラインの既定) */

/* 画素形式の 6Ah による指定は **PC-H98 専用** ([U] I/O 006Ah の
 * 0110001nb: 「■[PC-H98]」)。9821 では下の E0100h (MMIO) を使うこと。
 * しかも極性が逆 (6Ah 62h=プレーン/63h=パックド、E0100h 00h=パックド/
 * 01h=プレーン) なので取り違えると静かに壊れる。定数は置かない。 */

/* ------------------------------------------------------------------------ */
/*  2. モード読み戻し (I/O 09A0h)                                            */
/*                                                                          */
/*  出典 [B] §3-2-1 表3-3 / [U] I/O 09A0h。                                  */
/*  「調べたいモードの番号を 09A0h に書き、同じ 09A0h から読む。bit0 に       */
/*   0/1 が返る」。読み出し bit1 は GDC クロック (2.5/5.0MHz) で本件無関係。  */
/*  注意: PC-9821Ts にはこのポートが無く、As2 は常に FFh を返す設定がある。   */
/*  よって「1 が返った」だけでは検出にならない — 施錠/解錠を切り替えて        */
/*  0→1 が追随することを確かめる (backend_pegc.c の probe)。                 */
/* ------------------------------------------------------------------------ */
#define PEGC_STAT_PORT         0x09A0
#define PEGC_STAT_SEL_UNLOCK   0x08  /* 読むと bit0: 1=拡張モード変更可 */
#define PEGC_STAT_SEL_GFXMODE  0x0A  /* 読むと bit0: 1=拡張(256色) / 0=標準 */
#define PEGC_STAT_SEL_PAGECONT  0x0D /* 読むと bit0: 1=表裏ページ連続 */
/* 診断の 1 行 (backend_pegc.c pegc_diag_line) だけが読む選択 ([U] io_disp.md
 * I/O 09A0h の表): 02h = 奇数ラスタのマスク (68h 09h で 1)、03h = 画面表示
 * (68h 0Fh で 1)、05h = GDC 同期モード (6Ah 41h = プラズマ / LCD で 1)。 */
#define PEGC_STAT_SEL_ODDMASK  0x02
#define PEGC_STAT_SEL_DISP     0x03
#define PEGC_STAT_SEL_LCD      0x05
#define PEGC_STAT_BIT          0x01  /* 返り値のうち意味があるのは bit0 だけ */
/* GDC クロックの読み戻し ([U] io_disp.md I/O 09A0h、同 I/O 006Ah 82h〜85h)。
 *   09h を書いて読むと bit0 = GDC CLOCK-1 (0 = 82h 状態 2.5MHz / 1 = 83h 状態 5MHz)。
 *   読み値の bit1 は選択に関係なく GDC CLOCK-2 (0 = 84h 2.5MHz / 1 = 85h 5MHz)。
 * [B] 表3-3 には 09h の行が無い (UNDOCUMENTED を採る)。起動時の値を記録して、
 * 480 ラインから戻るときにそのクロックへ戻す (backend_pegc.c pegc_boot_sync_record)。 */
#define PEGC_STAT_SEL_GDCCLK1  0x09  /* 読むと bit0: GDC CLOCK-1 = 5MHz */
#define PEGC_STAT_RD_GDCCLK2   0x02  /* 読み値 bit1: GDC CLOCK-2 = 5MHz */

/* ------------------------------------------------------------------------ */
/*  3. 水平走査周波数 (I/O 09A8h)                                            */
/*                                                                          */
/*  出典 [U] I/O 09A8h「ノーマルモード水平走査周波数設定」[READ/WRITE]。      */
/*  bit1,0: 00b=24.83kHz / 01b=31.47kHz (10b/11b は設定禁止)。               */
/*  [B] §3-2-1 の本文は「垂直同期周波数」と書くが、同ファイルの表3-1 と       */
/*  UNDOCUMENTED は水平。UNDOCUMENTED を採る。                               */
/*  640x480 は 31kHz のときだけ指定できる ([B] INT 18h AH=30h の解説)。      */
/*  ⚠ [U] の解説:「周波数を切り替えたら GDC の SYNC コマンド等で同期信号を   */
/*  設定しなおさないと正常に表示されない」。**実測でもそのとおりだった**:     */
/*  09A8h と 6Ah (800 ライン構成) だけでは 400 ラインのままで、GDC の SYNC を */
/*  入れて初めて 480 ラインになる (票 H2c)。パラメータは下の §9。            */
/* ------------------------------------------------------------------------ */
#define PEGC_HSYNC_PORT        0x09A8
#define PEGC_HSYNC_24KHZ       0x00  /* 24.83kHz (640x400 の既定) */
#define PEGC_HSYNC_31KHZ       0x01  /* 31.47kHz (640x480 に必須) */
#define PEGC_HSYNC_MASK        0x03  /* 有効なのは bit1,0 */
/* 書くのは bit1,0 だけで、bit7〜2 は 0 ([U] io_disp.md I/O 09A8h「bit 7〜2:
 * 未使用 (常に 0 にする)」)。**読み値を書き戻さない** — 実機 Ra266 の起動時の
 * 読みは 81h (bit7=1、2026-09-29 の boot.log)。[U] 同項の解説は「Bp･Bs･Be･Bf･
 * Xt･Xa･Xn･Xp･Xs･Xe, BA2･BS2･BX2･BA3･BX3･BX4 はプログラマブル PLL シンセサイザ
 * (ドットクロック) のアクセスにもこのポートを使うが、通常はマスクされている」。
 * Ra266 はその機種表に無く、bit7 の意味は資料に無い。読んだ bit7 を書き戻すと
 * マスクされていない機種で PLL を叩きうるので、資料どおり 0 を書く。 */
/* 読み戻し ([B] §3-2 表3-1「リード 09A8H 水平同期周波数の読み出し」)。
 * 読み出しで定義があるのは **D0 (HF) だけ** で、D7〜D1 は「×」= 不定。
 * **読み値ではポートの有無を判定しない** (FFh も D0=1 の正常な読みであり
 * うる。レビュー往復 2)。有無は PEGC probe が通ったこと (= 9821 系で 09A0h
 * が 6Ah に追随し、F00000h の窓が書き読みできた) で決め、D0 だけを採る。
 * 裏付けと限界は backend_pegc.c の pegc_boot_sync_record の注記。
 * 起動時の値を記録し、480 ラインから戻るときにその値へ戻す (実機 Ra266 +
 * 液晶の桁ズレ、TASK_FDC_REALHW §9-1)。 */
#define PEGC_HSYNC_READ_HF     0x01  /* 読み出しの D0: 1=31.47kHz */

/* ------------------------------------------------------------------------ */
/*  4. メモリマップト I/O (E0000h セグメント)                                */
/*                                                                          */
/*  出典 [B] §3-2-3 表3-4「MATEのメモリマップトI/O」。                        */
/*  拡張グラフィックモードでは、標準モードでプレーン 3 (輝度) だった          */
/*  E0000h〜E7FFFh が **まるごと制御レジスタ** に変わる。                    */
/*                                                                          */
/*  ⚠ **これが H2 最大の罠**: `out 6Ah,21h` の副作用として物理 E0000h の      */
/*  意味が変わる。E0000h をまだ VRAM プレーンだと思っているコード (16 色の    */
/*  描画経路、退避しておいたフレームバッファポインタ) は制御レジスタを        */
/*  踏む。モード切替の前後で E0000h に触れないこと。                         */
/*  BIOS ROM は E8000h からなので MMIO 窓 (〜E7FFFh) とは重ならない [BM]。    */
/*                                                                          */
/*  データ幅は表3-4 のとおり: E0004h/E0006h/E0102h は 2 バイト、E0100h は     */
/*  1 バイト。                                                               */
/* ------------------------------------------------------------------------ */
#define PEGC_MMIO_BASE         0xE0000UL
#define PEGC_MMIO_BANK0        (PEGC_MMIO_BASE + 0x0004UL) /* u16: A8000h 窓のバンク 0-0Fh */
#define PEGC_MMIO_BANK1        (PEGC_MMIO_BASE + 0x0006UL) /* u16: B0000h 窓のバンク 0-0Fh */
#define PEGC_MMIO_PIXFMT       (PEGC_MMIO_BASE + 0x0100UL) /* u8 */
#define PEGC_MMIO_LINEAR       (PEGC_MMIO_BASE + 0x0102UL) /* u16 */

#define PEGC_PIXFMT_PACKED     0x00  /* パックトピクセル (1 バイト = 1 ドット) */
#define PEGC_PIXFMT_PLANE      0x01  /* プレーン */
#define PEGC_LINEAR_OFF        0x0000 /* F00000h に出現させない */
#define PEGC_LINEAR_ON         0x0001 /* F00000h に VRAM 全体 (512KB) を出現させる */

/* バンク窓 (A8000h / B0000h, 各 32KB)。**v1 では使わない**:
 * 51 ライン (32768/640) ごとに MMIO でバンクを切り替える必要があり、
 * present する矩形が途中で割れる (DESIGN §8)。定数は将来の 16MB 空間なし
 * 機種向けに残す。 */
#define PEGC_BANK_WIN0         0xA8000UL
#define PEGC_BANK_WIN1         0xB0000UL
#define PEGC_BANK_SIZE         0x8000UL   /* 32KB */
#define PEGC_BANK_MAX          0x0F       /* 512KB / 32KB - 1 */

/* ------------------------------------------------------------------------ */
/*  5. リニア窓 (F00000h) と 16MB システム空間 (I/O 043Bh)                   */
/*                                                                          */
/*  出典 [UM]「■16MB空間メモリ制御」/ I/O 043Bh、[BM] メモリマップ。         */
/*    - F00000h〜FFFFFFh の 1MB を「システムで使用」するか「通常の RAM」に    */
/*      するかを 043Bh bit2 が選ぶ。**bit2=0 がシステム使用 = PEGC VRAM が    */
/*      F00000h に見える**。bit2=1 だと F00000h からは見えなくなる           */
/*      (そのときは FFF00000h〜FFF7FFFFh で使える)。                          */
/*    - 拡張グラフィックス VRAM の実体は F00000h〜F7FFFFh の 512KB。          */
/*      FA0000h 以降は「0A0000〜0FFFFFh と同一の内容」のエイリアスなので      */
/*      フレームバッファとしてマップしてはいけない [BM]。                     */
/*    - 043Bh は [READ/WRITE] (書き込み専用ではない)。                        */
/*  ⚠ **PC-9801-61 型 SIMM 搭載機では 043Bh は「SIMM ソケットステータス」**   */
/*  という別のレジスタ ([UM] 同ファイル次節)。読み値だけで判断せず、          */
/*  最終判定は F00000h への書き込み読み戻しで行う (probe)。書き込みはしない。 */
/* ------------------------------------------------------------------------ */
#define PEGC_SYS16M_PORT       0x043B
#define PEGC_SYS16M_NORMAL_RAM 0x04  /* bit2: 1=通常のメモリ空間 / 0=システムが使用 */

#define PEGC_LINEAR_BASE       0x00F00000UL  /* リニア窓 (16MB システム空間の先頭) */
#define PEGC_LINEAR_SIZE       0x00080000UL  /* 拡張グラフィックス VRAM 512KB */
#define PEGC_LINEAR_ALT_BASE   0xFFF00000UL  /* bit2=1 のときの別名 (v1 未使用) */

/* ------------------------------------------------------------------------ */
/*  6. 画面ジオメトリ                                                        */
/*                                                                          */
/*  出典 [B] §3-2-3:「1 バイトが 1 ドットに対応するので … 1 ライン当たりの    */
/*  VRAM 容量は 640 バイト」「ADR = (VRAM の先頭アドレス) + y * 640 + x」。   */
/*  ※ 資料が 640 バイト/ラインを明示するのは 400 ライン時の文脈。480 ライン   */
/*  でのパディング無しは「パックドピクセルの帰結」からの推論であり、資料に    */
/*  明文は無い (G5 で実測確認する)。                                          */
/*  640x480 は 256 色専用、640x400 は 2/8/16/256 色 ([US] 0000:0597h bit1,0)。 */
/* ------------------------------------------------------------------------ */
#define PEGC_WIDTH             640
#define PEGC_HEIGHT_480        480
#define PEGC_HEIGHT_400        400
#define PEGC_BPP               8
#define PEGC_PITCH             PEGC_WIDTH            /* 1 バイト/画素 */
#define PEGC_FB_SIZE_480       (PEGC_PITCH * PEGC_HEIGHT_480)  /* 307200 */

/* ------------------------------------------------------------------------ */
/*  7. パレット (I/O A8h / AAh / ACh / AEh)                                  */
/*                                                                          */
/*  出典 [B] §3-2-1 / [U]「■パレットレジスタ(256色モード)」[READ/WRITE]。   */
/*  ポートは 16 色時と同じで、輝度だけ 0〜255 に拡張される。                  */
/*  **並びは G, R, B** (AAh=緑, ACh=赤, AEh=青) — RGB ではない。             */
/*  ポート番号自体は gfx/gfx.h の PAL_IDX_PORT / PAL_G_PORT / PAL_R_PORT /    */
/*  PAL_B_PORT が正典なので、ここでは重複定義せず色数だけを置く。            */
/*                                                                          */
/*  NP21/W での確認 (票 H2c):                                                */
/*    src/io/gdc.c gdc_oa8/gdc_oaa/gdc_oac/gdc_oae — 256 色モード             */
/*    (gdc.analog bit GDCANALOG_256) では A8h が索引 (0〜255)、AAh/ACh/AEh の */
/*    値が **8bit のまま** gdc.anareg[16*3 + idx*4 + 0..2] に入る。           */
/*    src/vram/palettes.c pal_make9821() がそれを np2_pal32[NP2PAL_GRPHEX+i]  */
/*    の p.g / p.r / p.b へ **無加工で** 代入する。                           */
/*    → レジスタは 0〜255。0〜15 を書くと 1/17 の輝度 = ほぼ黒になる。        */
/*  一方 OS32 の KAPI (gfx_set_palette / gfx_lease_palette) の輝度は          */
/*  **0〜15** (契約 G6/G8 の GuiRgb = PC-98 16 色の流儀)。                    */
/*  そこでバックエンドは KAPI 値を ×17 で 8bit へ伸ばす (15→255, 0→0)。      */
/* ------------------------------------------------------------------------ */
#define PEGC_PALETTE_COUNT     256
#define PEGC_PALETTE_MAX       255   /* 各輝度の最大値 (16 色機は 15) */
#define PEGC_PAL_KAPI_MASK     0x0F  /* KAPI 側の輝度は 4bit (0〜15) */
#define PEGC_PAL_KAPI_SCALE    17    /* 4bit → 8bit の伸張係数 (15*17 = 255) */

/* GUI がフォーカスアプリに貸せる範囲 (契約 G8)。0〜15 はシステム色として
 * 不可侵、16〜255 の 240 本を貸す。 */
#define PEGC_LEASE_FIRST       16
#define PEGC_LEASE_COUNT       (PEGC_PALETTE_COUNT - PEGC_LEASE_FIRST)

/* ------------------------------------------------------------------------ */
/*  8. 機種判別 (BIOS ワークエリア、物理 0000:0400h〜05FFh)                   */
/*                                                                          */
/*  出典 [US] docs/hw/undocumented/memsys.md。BIOS を呼ばずに読める。         */
/*    0000:045Ch bit6 「拡張グラフアーキテクチャ識別 1=拡張グラフ            */
/*                     アーキテクチャ機」(分類: 機種判別)                     */
/*                    ※ 98 ハイレゾボードのハイレゾモードでも 1 になるが     */
/*                       そのとき拡張グラフィックスは使えない。              */
/*    0000:0597h bit2 「拡張グラフィックスモード・GS 拡張グラフィックス      */
/*                     モードのサポート 1=あり」                             */
/*                bit1,0 現在のグラフィック解像度 (11b = 640x480)            */
/*    0000:0459h bit0 「CRT 解像度 480 ラインフラグ 1=640x480」              */
/*    0000:054Dh bit7  現在のグラフィックスモード (1=拡張グラフィックス)      */
/*  ページ 0 は paging_reclaim_conventional() で Read-Only present なので     */
/*  そのまま読める。                                                          */
/* ------------------------------------------------------------------------ */
#define PEGC_BIOS_ARCH_FLAG    0x045CUL
#define PEGC_BIOS_ARCH_EXTGFX  0x40   /* bit6: 拡張グラフアーキテクチャ機 */
#define PEGC_BIOS_MODE_FLAG    0x0597UL
#define PEGC_BIOS_MODE_EXTGFX  0x04   /* bit2: 拡張グラフィックスモード対応 */
#define PEGC_BIOS_MODE_RESMASK 0x03   /* bit1,0: 解像度 (11b=640x480) */
#define PEGC_BIOS_MODE_RES_480 0x03
#define PEGC_BIOS_CRT_EXT_STS  0x0459UL
#define PEGC_BIOS_CRT_480LINE  0x01   /* bit0: 1=640x480 */
#define PEGC_BIOS_PRXDUPD      0x054DUL
#define PEGC_BIOS_PRX_EXTGFX   0x80   /* bit7: 1=拡張グラフィックス (現在値) */
/* 054Dh bit2「GDC クロック 1=5.0MHz / 0=2.5MHz — 現在の GDC クロック周波数」
 * ([US] memsys.md PRXDUPD)。診断表示の突き合わせにだけ使う (採るのは 09A0h)。 */
#define PEGC_BIOS_PRX_GDC5M    0x04
/* 0000:054Ch PRXCRT bit5「水平走査周波数 [PC-9821(初代を除く), PC-H98]」
 * 0=24.83kHz(640x400) / 1=31.47kHz(640x400 または 640x480)。
 * ⚠ PC-9821 初代は 31kHz でも 0 のまま、対象外の機種は常に 0 ([US] 同項)。
 * なので **診断表示と、09A8h が読めなかったときの代わり** にだけ使う。 */
#define PEGC_BIOS_PRXCRT       0x054CUL
#define PEGC_BIOS_PRXCRT_31KHZ 0x20   /* bit5: 1=31.47kHz */

/* ------------------------------------------------------------------------ */
/*  9. GDC の表示タイミング (SYNC / SCROLL) — 480 ライン化 (票 H2c)           */
/*                                                                          */
/*  ⚠ **資料の欠落をエミュレータのソースで埋めた箇所**。ミラー [B]/[U] には   */
/*  480 ライン用の GDC SYNC パラメータ表が無い (400 ラインの PITCH 80/40 ワード*/
/*  しか載っていない)。検証対象は NP21/W なので、そちらの実装を正典として     */
/*  値を採った (docs/INDEX.md の「食い違ったら…」の適用: この環境では NP21/W)。*/
/*                                                                          */
/*  出典 (NP21/W ai-debug fork, ~/np21w-src/src/):                            */
/*    bios/bios18.c gdcmastersync[6][8] / gdcslavesync[6][8]                 */
/*      — INT 18h AH=42h (CRT モード設定) が GDC へ流す SYNC 8 バイト。       */
/*        master[2]="31"(400/31kHz), [5]="31-480:30", [1]="24"(400/24.83kHz)  */
/*        slave [1]="31-H"(480), [3]="24-M"(400/80 桁)                       */
/*    vram/dispsync.c dispsync_renewalvertical()                             */
/*      — 表示ライン数は SYNC の P7/P8 から計算される:                        */
/*        ymax = ((LOADINTELWORD(para+SYNC+6) - 1) & 0x3ff) + 1、             */
/*        vbp  = para[SYNC+7] >> 2、scrnymax = max(text,grph) を 8 で切り上げ。*/
/*        → 480 を出すには P7=E0h, P8=95h ((0x95E0-1)&0x3FF)+1 = 480。        */
/*        09A8h (31kHz) と 6Ah (800 ライン構成) だけでは scrnymax は 400 の    */
/*        まま = H2 で観測された症状。                                        */
/*    vram/makegrex.c makegrphex()/grphput_all()                             */
/*      — 256 色 (packed) の表示は GDC_SCROLL の SAD/LEN と GDC_PITCH で走査   */
/*        する。LEN が表示ライン数より小さいと下端がパーティション 1 の内容に  */
/*        化ける。値は実機の ROM の記録 (§10: SAD=0・LEN=3FFh・IM)。           */
/*        (uPD7220 の SCROLL P4 bit6 は IM — [B] 2-7「2.5MHz 時は 0、5MHz 時は */
/*        1」。)                                                              */
/*        PITCH と GDC クロックは §10 で**明示的に**入れる (2026-09-29〜)。     */
/*        以前は「BIOS 既定 40 + gdc.clock bit7=0 で実効 80 ワード」に頼って   */
/*        設定していなかったが、それは起動時の BIOS 状態への依存で、実機 Ra266  */
/*        で崩れる候補 (TASK_PEGC480_REALHW §2 H5)。NP21/W の実効 PITCH は     */
/*        vram/makegrex.c: clock が 83h (= 83h と 85h の両方) なら PITCH そのまま、*/
/*        それ以外は 2 倍。80 + 5MHz でも 40 + 2.5MHz でも 640 バイト/ライン。 */
/*  ※ P1〜P8 は uPD7220 SYNC の並び (P1=モード, P2=CR, P5=HBP, P7/P8=AL/VBP)。 */
/*    ポート番号は pc98.h の GDC_TEXT_CMD/PARAM (マスタ=テキスト) と          */
/*    GDC_GFX_CMD/PARAM (スレーブ=グラフィック) が正典。                      */
/* ------------------------------------------------------------------------ */
#define PEGC_GDC_SYNC_LEN      8    /* SYNC のパラメータ数 */
#define PEGC_GDC_SCROLL_LEN    4    /* SCROLL のパラメータ数 (パーティション 1 面) */

/* 値そのものは §10 (実機の記録で差し替える 1 か所) に置く。 */

/* ------------------------------------------------------------------------ */
/*  10. 表示タイミングの値 — **実機の記録で差し替える箇所はここだけ**        */
/*                                                                          */
/*  出所: **実機 PC-9821Ra266 の ROM の INT 18h AH=30h を V86 で記録**       */
/*  (`v86 -g`、2026-09-29 20:51、feat/gui d5cd3a5。票 TASK_PEGC480_REALHW §3 */
/*  「段 1 の実機での記録 (2 回目)」と §3-3 の比較表)。s480 = 640x480 へ、    */
/*  back = 起動時のモード (31kHz・GDC 2.5MHz・400 ライン 25 行) へ戻る列。   */
/*  資料に無い値なので、出典は「実機 Ra266 の ROM を V86 で記録」。          */
/*                                                                          */
/*  **SYNC の 8 バイトは NP21/W の値 (bios/bios18.c gdcmastersync /          */
/*  gdcslavesync) と 480・31kHz 400 (2.5MHz) の 4 組とも一致した** — 仮説 H1  */
/*  (SYNC の値の違い) は否定。違っていたのは順序と、OS32 が出していなかった */
/*  コマンド (RESET・MASTER/SLAVE・CSRFORM・ZOOM・テキスト GDC の PITCH と     */
/*  SCROLL・表示停止・6Ah 41h・6Eh・テキスト CRTC) と SCROLL の LEN。         */
/*  書く**順序**は backend_pegc.c の pegc_apply_timing() 1 か所。            */
/*                                                                          */
/*  記録に無い組 (起動時 24kHz・起動時 5MHz の戻り) は資料で埋める:           */
/*    24kHz の SYNC は [B] 2-6 表2-27 (= NP21/W の 24 / 24-L / 24-M)。       */
/*    31kHz・5MHz の戻りのグラフィック SYNC は NP21/W の 31-M (実機の記録    */
/*    なし)。SCROLL の IM (第 4 バイト bit6) は [B] 2-7「2.5MHz 時は 0、5MHz  */
/*    時は 1」、LEN は記録の 3FFh。                                          */
/* ------------------------------------------------------------------------ */

/* --- SYNC 8 バイト (P1〜P8、[B] 2-6 表2-26) --- */
/* 480 ライン (31.47kHz / 640x480、テキスト 16 ラスタ × 30 行)。
 * 実機 s480: 62h 0Eh + 10 4E 4B 0C 03 06 E0 95 / A2h 0Eh + 02 4E 4B 0C 83 06 E0 95 */
#define PEGC_GDC_MSYNC_480     { 0x10, 0x4E, 0x4B, 0x0C, 0x03, 0x06, 0xE0, 0x95 }
#define PEGC_GDC_SSYNC_480     { 0x02, 0x4E, 0x4B, 0x0C, 0x83, 0x06, 0xE0, 0x95 }

/* 400 ライン 24.83kHz (NP21/W の CUI、資料の標準)。実機の記録なし。
 * テキストは [B] 表2-27 テキスト (2.5MHz)、グラフィックは同表 2.5MHz / 5MHz。 */
#define PEGC_GDC_MSYNC_400         { 0x10, 0x4E, 0x07, 0x25, 0x07, 0x07, 0x90, 0x65 }
#define PEGC_GDC_SSYNC_400_2M5     { 0x02, 0x26, 0x03, 0x11, 0x83, 0x07, 0x90, 0x65 }
#define PEGC_GDC_SSYNC_400_5M      { 0x02, 0x4E, 0x07, 0x25, 0x87, 0x07, 0x90, 0x65 }

/* 400 ライン 31.47kHz (実機 Ra266 の CUI、boot.log `[pegc] hsync=31k`)。
 * 実機 back: 62h 0Eh + 10 4E 47 0C 07 0D 90 89 / A2h 0Eh + 02 26 41 0C 83 0D 90 89
 * (GDC 2.5MHz)。5MHz 用は記録なし — NP21/W gdcslavesync "31-M"。 */
#define PEGC_GDC_MSYNC_400_31K     { 0x10, 0x4E, 0x47, 0x0C, 0x07, 0x0D, 0x90, 0x89 }
#define PEGC_GDC_SSYNC_400_31K_2M5 { 0x02, 0x26, 0x41, 0x0C, 0x83, 0x0D, 0x90, 0x89 }
#define PEGC_GDC_SSYNC_400_31K_5M  { 0x02, 0x4E, 0x47, 0x0C, 0x87, 0x0D, 0x90, 0x89 }

/* --- CSRFORM 3 バイト ([B] 2-6 表2-26: P1 = CS | L/R、P3 = CFI<<3 | BLh) ---
 * 実機は s480・back とも同じ値。
 * テキスト 0F 00 7B: L/R = 0Fh (16 ラスタ/行)、CS = 0 (カーソル非表示)、
 *   CFI = 0Fh、BLh = 3 (NP21/W bios18.c の (raster << 3) + 3 と同じ形)。
 *   カーソルの表示はコンソールの持ち物なので、CUI へ戻った後で
 *   console_hw_cursor_enable() が改めて入れる (backend_pegc.c)。
 * グラフィック 00 00 01: L/R = 0 (1 ラスタ = 1 ライン。200 ラインの縦 2 倍を
 *   使わない — [U] io_disp.md I/O 0068h 08h/09h の注)。OS32 は以前これを書かず、
 *   起動時の BIOS の値に頼っていた (NP21/W の 256 色表示は L/R を見ない)。 */
#define PEGC_GDC_TCSRFORM          { 0x0F, 0x00, 0x7B }
#define PEGC_GDC_GCSRFORM          { 0x00, 0x00, 0x01 }
#define PEGC_GDC_CSRFORM_LEN       3

/* --- ZOOM 1 バイト ([B] 2-7: ZR = 0 以外不可) --- 両 GDC とも 00h (実機)。 */
#define PEGC_GDC_ZOOM              0x00

/* --- PITCH (ワード) ---
 * グラフィック: 480 は 80 (実機 s480 A2h 47h + 50h、[B] 2-7「5MHz 時は 80」)。
 * テキスト: 80 (実機 s480・back とも 62h 47h + 50h、[B] 2-6「通常は 80」)。 */
#define PEGC_GDC_PITCH_480         80
#define PEGC_GDC_TPITCH            80

/* 400 ラインへ戻るときのグラフィックの PITCH。**PITCH は書き込み専用で読み
 * 戻せない** (uPD7220 の読み出しは READ/LPEN/CSRR だけ) ので、起動時に**読める**
 * GDC クロック (§2 の 09A0h) から [B] 2-7 の「通常」の値を選ぶ: 両方 5MHz なら 80、
 * それ以外 (どちらか一方でも 2.5MHz — [U] 006Ah 82h の注) は 40。
 * 実機 back (2.5MHz) は A2h 47h + 28h = 40 で一致。 */
#define PEGC_GDC_PITCH_400_2M5     40
#define PEGC_GDC_PITCH_400_5M      80

/* --- SCROLL 4 バイト (パーティション 1 面: SAD 下位・中位、LEN 下位 4 ビット<<4 |
 * SAD 上位、IM<<6 | LEN 上位 6 ビット — uPD7220 の PRAM の並び) ---
 * 実機はどれも SAD = 0・**LEN = 3FFh** (P3 = F0h、P4 下位 6 ビット = 3Fh)。
 * OS32 は以前 LEN = 0 (00 00 00 40 / 00 00 00 00) を入れていた (NP21/W の BIOS
 * と同じ。NP21/W の vram/makegrex.c は LEN = 0 を「無制限」に読む)。
 *   グラフィック 480:  00 00 F0 7F (IM = 1 = 5MHz) — 実機 s480
 *   グラフィック 400 2.5MHz: 00 00 F0 3F (IM = 0) — 実機 back
 *   グラフィック 400 5MHz:   00 00 F0 7F (IM = 1) — 記録なし、[B] 2-7 の IM
 *   テキスト (両方):   00 00 F0 3F (テキスト GDC は IM = 0) — 実機 s480・back */
#define PEGC_GDC_SCROLL_480        { 0x00, 0x00, 0xF0, 0x7F }
#define PEGC_GDC_SCROLL_400_2M5    { 0x00, 0x00, 0xF0, 0x3F }
#define PEGC_GDC_SCROLL_400_5M     { 0x00, 0x00, 0xF0, 0x7F }
#define PEGC_GDC_TSCROLL           { 0x00, 0x00, 0xF0, 0x3F }

/* --- GDC クロック (§11 の 6Ah の値) --- 640x480 は 5MHz 固定 ([US] 054Dh bit2)。
 * 実機 s480: 6Ah 83h, 85h (両方)。back (起動時 2.5MHz): 6Ah 82h, 84h (両方)。 */
#define PEGC_GDC_CLK1_480          PEGC_FF2_GDC_CLK1_5M
#define PEGC_GDC_CLK2_480          PEGC_FF2_GDC_CLK2_5M

/* --- テキスト CRTC (I/O 70h〜7Ah、[B] 2-6-4 表2-28 / [U] io_disp.md) ---
 * 実機 s480・back とも PL 00h・BL 0Fh・CL 10h・SSL 00h・SUR 01h・SDR 00h
 * (NP21/W bios18.c crtdata の "400-25" / "480-30" とも同じ)。書く順は 70h→7Ah。 */
#define PEGC_CRTC_VALUES           { 0x00, 0x0F, 0x10, 0x00, 0x01, 0x00 }
#define PEGC_CRTC_COUNT            6

/* ------------------------------------------------------------------------ */
/*  11. GDC クロック (I/O 6Ah 82h〜85h) と PITCH コマンド                    */
/*                                                                          */
/*  出典 [U] io_disp.md I/O 006Ah「1000001nb: GDC CLOCK-1」「1000010nb: GDC   */
/*  CLOCK-2」■[ノーマル]、[B] 3-2 表3-2 (84H / 83H と 85H)。                */
/*  5MHz には CLOCK-1・2 の**両方**を 5MHz に、2.5MHz にはどちらか一方でよい。*/
/*  [B] 表3-2 は 07h の解錠を要する値に * を付けるが、82h〜85h には無い      */
/*  (NP21/W io/gdc.c gdc_o6a も解錠を見ない) — 解錠せずに直接書く。         */
/*  PITCH (47h) は pc98.h の GDC_CMD_PITCH、パラメータ 1 バイト。           */
/* ------------------------------------------------------------------------ */
#define PEGC_FF2_GDC_CLK1_2M5  0x82
#define PEGC_FF2_GDC_CLK1_5M   0x83
#define PEGC_FF2_GDC_CLK2_2M5  0x84
#define PEGC_FF2_GDC_CLK2_5M   0x85
#define PEGC_GDC_PITCH_LEN     1

/* ------------------------------------------------------------------------ */
/*  12. GDC の FIFO を待つ上限 (票 TASK_PEGC480_REALHW §4、仮説 H3)          */
/*                                                                          */
/*  uPD7220 のステータス (60h / A0h の READ、pc98.h GDC_STAT_*) の            */
/*  FIFO FULL (bit1) / FIFO EMPTY (bit2) を見てから書く。コマンドの前は       */
/*  EMPTY (前のコマンドを GDC が食べ終わった)、パラメータの前は FULL でない  */
/*  こと。1 回読んで満たなければ PEGC_GDC_FIFO_POLL_US 待って読み直し、      */
/*  PEGC_GDC_FIFO_POLLS 回で諦めて書く (数えて先へ — 画面の設定を途中で      */
/*  止めるより、書いて後で数を見る方が戻れる)。上限は 1 バイトあたり         */
/*  2µs × 5000 = 10ms。SYNC などの表示制御コマンドは GDC のクロックで数十     */
/*  サイクルなので桁違いに余裕がある。cpu_delay_us は校正前だと 0 で返るが、 */
/*  回数で打ち切るので無限ループにはならない。NP21/W はステータスを読むと    */
/*  FIFO を処理するので 1〜2 回で抜ける (io/gdc.c gdc_i60 / gdc_ia0)。       */
/* ------------------------------------------------------------------------ */
#define PEGC_GDC_FIFO_POLL_US  2
#define PEGC_GDC_FIFO_POLLS    5000

/* ------------------------------------------------------------------------ */
/*  13. 実機の ROM の列に合わせるために足したポートとコマンド                 */
/*                                                                          */
/*  どれも実機 Ra266 の ROM が s480・back の両方で出していたもの (票         */
/*  TASK_PEGC480_REALHW §3-3)。意味は資料から:                               */
/*    6Ah 41h: [B] 3-2 表3-2「41H プラズマディスプレイモード — テキスト画面の  */
/*      1 ドットの横ずれの制御」、[U] io_disp.md I/O 006Ah 0100000nb「拡張    */
/*      グラフィックスモードでは、必ずプラズマディスプレイモードになる」。   */
/*      NP21/W の BIOS は 40h (CRT モード) を出す — 実機に合わせて 41h。      */
/*    6Eh 03h / 21h / 02h: [U] io_disp.md I/O 006Eh (拡張アトリビュート、     */
/*      PC-H98・PC-9821・BA2 等) の 0000001nb「02h = モード変更禁止 /       */
/*      03h = 許可」と 0010000nb「20h = 不明 (24kHz の時) / 21h = 不明        */
/*      (31kHz の時)」「OUT 時には OUT 5Fh,AL によるウェイトが必要」。       */
/*      実機は 31kHz の s480・back とも 21h。24kHz へ戻る機種は資料の 20h。   */
/*      ⚠ PC-9801 初代〜の一部では同じ 6Eh が「モニタ周波数切換」(00h=15kHz) */
/*      ([U] 同ファイルの 1 つ目の I/O 006Eh) なので、PEGC の probe が通った  */
/*      機種 (9821) でしか書かない。                                         */
/*    GDC 05h (STOP2)・6Bh (START): [U] io_disp.md I/O 0062h / 00A2h の      */
/*      コマンド表 (05h STOP2、0Dh/6Bh START)。RESET1 (00h) の後に表示を      */
/*      始めるのは実機の ROM も 6Bh。                                        */
/*  VSYNC 待ち: 実機の ROM は群の間で 60h を約 1 フレームぶん (V86 の #GP    */
/*  経由で 1500〜5800 回) 読み続け、最後の読みは VSYNC (bit5) が落ちた値     */
/*  (45h) だった → 「VSYNC が来るまで待ち、明けるまで待つ」と読んだ (ROM の  */
/*  命令列は逆アセンブルしていない — 推定)。1 辺ごとの上限は               */
/*  PEGC_VSYNC_POLL_US × PEGC_VSYNC_POLLS = 50ms (24kHz 400 ラインの 1 フレーム */
/*  約 18ms の 2 倍超)。上限に達したら数えて先へ (pegc_vsync_timeouts)。     */
/* ------------------------------------------------------------------------ */
#define PEGC_FF2_LCD_MODE      0x41  /* 6Ah: プラズマディスプレイ / LCD モード */
#define PEGC_XATTR_PORT        0x6E  /* 拡張アトリビュート (9821 系) */
#define PEGC_XATTR_UNLOCK      0x03  /* モード変更許可 */
#define PEGC_XATTR_LOCK        0x02  /* モード変更禁止 */
#define PEGC_XATTR_24KHZ       0x20  /* 不明 (24kHz の時) */
#define PEGC_XATTR_31KHZ       0x21  /* 不明 (31kHz の時) */
#define PEGC_GDC_CMD_STOP2     0x05  /* [U] 0062h 表 STOP2 */
#define PEGC_GDC_CMD_START2    0x6B  /* [U] 0062h 表 START (6Bh) */
#define PEGC_VSYNC_POLL_US     10
#define PEGC_VSYNC_POLLS       5000

/* 480 ラインでのテキスト行数 (16 ラスタ/行)。モード切替時にここまでクリアして
 * おかないと 25 行目以降に古い内容が残り、テキスト面がグラフィックを隠す
 * (NP21/W vram/sdrawex.mcr の pex_2: テキストが 0 以外の画素はテキスト優先)。 */
#define PEGC_TEXT_ROWS_480     30

#endif /* __PEGC_H */
