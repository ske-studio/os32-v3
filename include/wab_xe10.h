/* ======================================================================== */
/*  WAB_XE10.H — PC-9821Xe10 内蔵ウィンドウアクセラレータのポート/窓定数     */
/*                                                                          */
/*  「ボードグルー」層の番地はすべてこのファイルに集める ([C4] 三層定数 /     */
/*  票 H3 の鉄則)。**チップドライバ (drivers/wab_cirrus.c) はこのヘッダを     */
/*  include しない** — チップ側は VGA の論理レジスタ番号 (3C0h 系) だけを     */
/*  知り、PC-98 のポート番号は一切持たない (DESIGN §6, §7-3)。               */
/*                                                                          */
/*  出典 (docs/hw/ = ローカルミラー。著作物なので git 管理外):                */
/*    [W]  docs/hw/undocumented/io_wab.md                                    */
/*         「ウィンドウアクセラレータ制御」/ I/O 0FAAh・0FABh (内蔵制御)、    */
/*         0904h・0CA0h〜0CAFh・0DA4h/0DA5h/0DAAh・FF82h (CL-GD5428,5430 の   */
/*         VGA レジスタ写像)、内部レジスタ 00h(ID) 01h(VRAM 窓) 02h 03h(リレー)*/
/*    [N]  NP21/W ai-debug fork ~/np21w-src/src/                             */
/*         wab/cirrus_vga.c cirrusvga_ofaa/ifaa/ofab/ifab (2 段 I/O の実装)、 */
/*         pc98_cirrus_vga_initVRAMWindowAddr() (内蔵型の既定窓 = F60000h)、  */
/*         wab/cirrus_vga_extern.h VRA2WINDOW_SIZEX (窓サイズ 32KB)、         */
/*         i286c/cpumem.c memp_read8/write8 (窓の張り方)                      */
/*  資料と NP21/W が食い違う点は各定数のコメントに明記する (docs/INDEX.md の  */
/*  「食い違ったら…」: この環境の検証対象は NP21/W なのでそちらを採る)。      */
/* ======================================================================== */

#ifndef __WAB_XE10_H
#define __WAB_XE10_H

#include "types.h"
#include "memmap.h"   /* MEM_DEVICE_APERTURE_* (リニア窓の置き場) */

/* ------------------------------------------------------------------------ */
/*  1. 内蔵アクセラレータ制御 (2 段 I/O)                                     */
/*                                                                          */
/*  出典 [W] I/O 0FAAh [INDEX] / 0FABh [DATA]。                              */
/*  ボード型 (PC-9801-85/91/96 等) は 0FA2h/0FA3h だが、Xe10 は**内蔵型**な   */
/*  ので 0FAAh/0FABh。ボード側のポートはこのグルーでは扱わない。              */
/*  ⚠ 40E1h / 42E1h / 46E8h / 51E1h / 5BE1h は**メルコ / I-O DATA の C バス  */
/*  製ボード専用**で Xe10 のポートではない (DESIGN §3.2, 2026-09-04 訂正)。   */
/*  絶対に触らないこと。                                                     */
/* ------------------------------------------------------------------------ */
#define WAB_XE10_INDEX_PORT     0x0FAA  /* 内部レジスタ番号 [READ/WRITE] */
#define WAB_XE10_DATA_PORT      0x0FAB  /* 内部レジスタのデータ [READ/WRITE] */

/* 内部レジスタ番号 ([W] 0FABh の表) */
#define WAB_XE10_REG_ID         0x00  /* R: ID 値 */
#define WAB_XE10_REG_VRAMWIN    0x01  /* R/W: VRAM ウィンドウアドレス */
#define WAB_XE10_REG_LINEARWIN  0x02  /* R/W: 資料は S3 機のみ。§4 参照 */
#define WAB_XE10_REG_RELAY      0x03  /* R/W: 映像出力リレー / MMIO 制御 */
#define WAB_XE10_REG_DIPSW      0x04  /* R: ディップスイッチ設定 */

/* ID 値 ([W] 0FABh レジスタ 00h の表)。
 * 5Bh = PC-9821Xe10 / Xa7e / Xb10 / PC-9801BX4 内蔵アクセラレータ。
 * NP21/W も同じ値を返す ([N] cirrusvga_ifab case 0x00 →
 * (REG8)np2clvga.gd54xxtype、cirrus_vga_extern.h CIRRUS_98ID_Xe10 = 0x5B)。
 * probe はこの 1 値だけを受け入れる (票 H3 作業 1)。他機種 (Xe=58h, Cb=59h,
 * Cf=5Ah, Cb2=5Ch, Cx2=5Dh) も同じ CL-GD5430 だが、窓の既定やチップ ID が
 * 検証できていないので v1 では受け付けない。 */
#define WAB_XE10_ID_XE10        0x5B

/* リレー制御レジスタ (03h) のビット ([W] 0FABh レジスタ 03h)。
 * bit1: 1 = アクセラレータ出力 / 0 = 本体 (98 グラフィック) スルー
 * bit0: 1 = メモリマップト I/O イネーブル / 0 = ディセーブル
 * ⚠ NP21/W では bit0 は np2clvga.mmioenable に入るだけで、**内蔵型には MMIO
 * 窓が無い** ([N] pc98_cirrus_vga_initVRAMWindowAddr は gd54xxtype<=0xff の
 * とき pciMMIO_Addr を 0 のままにする → i286c/cpumem.c の MMIO 分岐に入らない)。
 * よって OS32 は bit0 を 0 のままにし、BLT レジスタは I/O 経由で設定する。 */
#define WAB_XE10_RELAY_ACCEL    0x02  /* bit1: 映像出力をアクセラレータへ */
#define WAB_XE10_RELAY_MMIO     0x01  /* bit0: MMIO 窓を有効化 (v1 未使用) */

/* ------------------------------------------------------------------------ */
/*  2. VGA レジスタの PC-98 への写像                                          */
/*                                                                          */
/*  出典 [W]「本体内蔵 CL-GD5428,5430 の制御は I/O 0904h, 0CA0〜0CAFh,        */
/*  0DA4h, 0DA5h, 0DAAh, FF82h が使用されている」および各ポートの項           */
/*  (0CA0h=03C0h, 0CA4h=03C4h, 0CA5h=03C5h, 0CA6h=03C6h, 0CA7h=03C7h,        */
/*   0CA8h=03C8h, 0CA9h=03C9h, 0CAEh=03CEh, 0CAFh=03CFh, 0DA4h=03D4h,        */
/*   0DA5h=03D5h, 0DAAh=03DAh)。                                             */
/*  規則は単純で、**下位ニブルはそのまま・上位を差し替える**だけ:             */
/*    03C0h〜03CFh → 0CA0h + (reg & 0x0F)                                    */
/*    03D0h〜03DFh → 0DA0h + (reg & 0x0F)                                    */
/*    03B0h〜03BFh → 0BA0h + (reg & 0x0F)  (モノクロ別名。DESIGN §4 が        */
/*                    NP21/W のポート表で確認した 0BA4h/0BA5h/0BAAh に一致)   */
/*  POS レジスタだけ規則から外れる:                                          */
/*    ネイティブ 0094h → 0904h ([W] I/O 0904h)                               */
/*    ネイティブ 0102h → FF82h ([W] I/O FF82h)                               */
/* ------------------------------------------------------------------------ */
#define WAB_XE10_VGA_3CX_BASE   0x0CA0  /* 03C0h〜03CFh の写像先 */
#define WAB_XE10_VGA_3DX_BASE   0x0DA0  /* 03D0h〜03DFh の写像先 */
#define WAB_XE10_VGA_3BX_BASE   0x0BA0  /* 03B0h〜03BFh の写像先 */
#define WAB_XE10_VGA_GROUP_MASK 0x0FF0  /* 論理ポートの「3Cx / 3Dx / 3Bx」判定 */
#define WAB_XE10_VGA_LOW_MASK   0x000F  /* 下位ニブルはそのまま流す */

#define WAB_XE10_POS94_PORT     0x0904  /* ネイティブ 0094h (102Access Control) */
#define WAB_XE10_POS102_PORT    0xFF82  /* ネイティブ 0102h (Video Subsystem Enable) */
#define WAB_XE10_POS102_ENABLE  0x01    /* bit0: Video Subsystem Enable */
#define WAB_XE10_POS94_ACCESS   0x20    /* bit5: POS 102 Access */

/* ------------------------------------------------------------------------ */
/*  3. VRAM ウィンドウ (CPU から見える窓)                                     */
/*                                                                          */
/*  出典 [W] 0FABh レジスタ 01h「VRAM ウィンドウアドレス」:                   */
/*    10h= 0B0000〜0BFFFFh / 80h= F20000〜F2FFFFh / A0h= F00000〜F0FFFFh /    */
/*    C0h= F40000〜F4FFFFh / E0h= F60000〜F6FFFFh 「この値は、ITF でセット」  */
/*  NP21/W も同じ符号化 ([N] cirrusvga_ofab case 0x01) で、**内蔵型の既定は    */
/*  F60000h** ([N] pc98_cirrus_vga_initVRAMWindowAddr の gd54xxtype<=0xff 枝  */
/*  → VRAMWindowAddr2 = 0xf60000)。                                          */
/*                                                                          */
/*  ⚠ **窓は 32KB しかない**。資料の表記は 64KB 幅 (F60000〜F6FFFFh) だが、   */
/*  NP21/W の実装は 32KB 単位で判定する ([N] cirrus_vga_extern.h              */
/*  VRA2WINDOW_SIZEX = 0x8000、CIRRUS_VRAMWINDOW2MASK、および                 */
/*  cirrus_linear_memwnd_addr_convert の `addr &= 0x7fff`)。窓内のオフセット  */
/*  はチップのバンクレジスタ (GR09/GR0A/GR0B) が VRAM のどこを指すかで決まる。 */
/*  → 300KB のクライアント面をこの窓から一望することはできない。              */
/*  **H3b 以降、backend_cirrus はこの窓を使わない** — CPU から触るのは §4 の   */
/*  リニア窓 (2MB) で、バンク切替そのものが要らなくなった。この定数群は        */
/*  グルーが reg 01h を既定値 (ITF の設定) に固定するためだけに残っている。    */
/*                                                                          */
/*  PEGC のリニア窓 (F00000h) と場所が重なりうるので、既定の F60000h をその   */
/*  まま使う。PEGC と Cirrus は同時に選ばれない (バックエンドは 1 枚) が、     */
/*  probe が両方走る可能性があるため窓は分けておく。 */
/* ------------------------------------------------------------------------ */
#define WAB_XE10_VRAMWIN_B0000  0x10  /* 0B0000h (98 の VRAM と重なる。使わない) */
#define WAB_XE10_VRAMWIN_F20000 0x80
#define WAB_XE10_VRAMWIN_F00000 0xA0
#define WAB_XE10_VRAMWIN_F40000 0xC0
#define WAB_XE10_VRAMWIN_F60000 0xE0

#define WAB_XE10_WIN_BASE_F20000 0x00F20000UL
#define WAB_XE10_WIN_BASE_F00000 0x00F00000UL
#define WAB_XE10_WIN_BASE_F40000 0x00F40000UL
#define WAB_XE10_WIN_BASE_F60000 0x00F60000UL

/* OS32 が使う窓 (既定値をそのまま採用する = ITF の設定を壊さない) */
#define WAB_XE10_WIN_SEL        WAB_XE10_VRAMWIN_F60000
#define WAB_XE10_WIN_BASE       WAB_XE10_WIN_BASE_F60000
#define WAB_XE10_WIN_SIZE       0x8000UL   /* 32KB ([N] VRA2WINDOW_SIZEX) */

/* ------------------------------------------------------------------------ */
/*  4. 「リニア VRAM アクセス用アドレス」(レジスタ 02h) — H3b で採用          */
/*                                                                          */
/*  [W] は 0FABh の 02h を「VRAM ウィンドウアドレス ■[PC-9821Ap2/As2/An/     */
/*  Xs/Xp/Xn/Ap3/As3]」= S3 搭載機用としか書いておらず、CL-GD5430 内蔵機での   */
/*  意味は**資料に無い**。NP21/W はこれを全内蔵型で受け付け、                  */
/*  `VRAMWindowAddr = dat << 24` として **2MB のリニア窓** を開く              */
/*  ([N] cirrusvga_ofab case 0x02、cirrus_vga_extern.h VRAMWINDOW_SIZE =      */
/*   0x200000、i386c/cpumem.c memp_read8 の vramWndAddr 分岐)。               */
/*                                                                          */
/*  窓は §3 のバンク窓と違い **バンクレジスタ (GR09) を通らない** ([N]        */
/*  cirrus_linear_readb / _writeb は addr &= cirrus_addr_mask するだけ。      */
/*  GR09 を見る cirrus_linear_memwnd_addr_convert は 01h の窓専用)。          */
/*  つまり窓の先頭 = VRAM オフセット 0 の線形写像で、300KB のクライアント面を  */
/*  CPU から一望できる。SR4 bit3 (chain4) を立てた 8bpp では 1 バイト 1 画素。 */
/*                                                                          */
/*  値は dat<<24 = **16MB 単位**。NP21/W は窓を VRAMWINDOW_SIZE +            */
/*  EXT_WINDOW_SIZE = **4MB** の範囲で出す ([N] cirrus_vga.c の               */
/*  pc98_cirrus_vga_update_memp_map、読み書きは RAM より先に窓を判定する)。   */
/*  窓の番地は OS が選ぶ (2026-09-29、v3 のデバイス窓の帯へ移設):             */
/*    - かつての dat = 01h (01000000h) は 16MB 超の RAM と重なる。NP21/W      */
/*      17MB 構成で probe が落ち、実機 64MB (16MB から RAM が連続) でも同じ。 */
/*    - 512MB 付近 (dat = 20h) は PCI の BIOS が BAR を割り当てる帯           */
/*      (Ra266 の実測 0x20000000〜、TASK_LAN_82557 §6)。                     */
/*    - dat = FFh は NP21/W が捨てる。**dat = FEh (FE000000h) が置ける最上位** */
/*      で、include/memmap.h の MEM_DEVICE_APERTURE_* の帯の先頭に当たる。   */
/*  この窓は **NP21/W 互換のためだけ** (ユーザー決定 2026-09-29。実機の       */
/*  Xe10 がレジスタ 02h に応えるかは資料に無く未確認。手元の実機 Ra266 の    */
/*  アクセラレータは PCI の Trident で、Xe10 の Cirrus は無い)。              */
/*  帯の PT は paging_init が静的に用意する (kernel/paging.c)。               */
/*                                                                          */
/*  ⚠ **窓を閉じる書き込みは NP21/W では効かない**: cirrusvga_ofab case 0x02   */
/*  は `if(dat!=0x00 && dat!=0xff)` で 00h/FFh を捨てるので、一度開いた窓は    */
/*  VRAMWindowAddr が残る。OS32 側の後始末は「ページを Not-Present に戻す」    */
/*  ことで行い、レジスタへの 0 書き込みは実機向けの作法として併せて出す。      */
/* ------------------------------------------------------------------------ */
#define WAB_XE10_LINEARWIN_SHIFT 24          /* dat << 24 が窓の物理先頭 */
#define WAB_XE10_LINEARWIN_SIZE  0x00200000UL /* 2MB ([N] VRAMWINDOW_SIZE) */

/* NP21/W が窓として出す範囲 (VRAMWINDOW_SIZE + EXT_WINDOW_SIZE)。描画に使う
 * のは先頭の WAB_XE10_LINEARWIN_SIZE だけだが、窓の帯にはこの全体が収まる
 * こと (backend_cirrus.c の STATIC_ASSERT)。 */
#define WAB_XE10_LINEARWIN_DECODE 0x00400000UL /* 4MB */

/* OS32 が使うリニア窓。dat = FEh → FE000000h (v3 のデバイス窓の帯の先頭)。
 * RAM の量に関係なく RAM とは重ならない (帯は物理地図で MMIO)。 */
#define WAB_XE10_LINEARWIN_SEL \
    ((u8)(MEM_DEVICE_APERTURE_BASE >> WAB_XE10_LINEARWIN_SHIFT))
#define WAB_XE10_LINEARWIN_OFF   0x00        /* 窓を閉じる値 (NP21/W は無視) */
#define WAB_XE10_LINEARWIN_BASE \
    ((u32)WAB_XE10_LINEARWIN_SEL << WAB_XE10_LINEARWIN_SHIFT)

#endif /* __WAB_XE10_H */
