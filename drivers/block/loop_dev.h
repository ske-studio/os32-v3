/* ======================================================================== */
/*  LOOP_DEV.H — 汎用ループバックブロックデバイス                            */
/*                                                                          */
/*  ディスクイメージファイル (D88/FDI/HDI/RAW等) を lo0..lo3 ブロック        */
/*  デバイスとして公開する。拡張子でフォーマットを判別。                      */
/*                                                                          */
/*  CHS コア設計: PC-98 のハードウェアは全て CHS ネイティブであるため、      */
/*  CHS レベルの API をコアとして公開し、LBA は薄いラッパーで提供する。      */
/* ======================================================================== */

#ifndef LOOP_DEV_H
#define LOOP_DEV_H

#include "loop_contract.h"

/* fd ベースのアタッチ (fmt でフォーマット指定)
 * 既に vfs_open 済みの fd を直接スロットにアタッチする。
 * fd の所有権は呼び出し側に残る (detach 時に close しない)。
 * fmt: LOOP_FMT_D88 / LOOP_FMT_FDI / LOOP_FMT_HDI 等
 * 戻り値: 0=成功, -1=fd/スロット不正, -2=フォーマット不正,
 *         -3=スロット使用中, -4=I/Oエラー */
int  loop_dev_attach_fd(int fd, int slot, int fmt);

/* フォーマット種別定数 */
#define LOOP_FMT_NONE     0
#define LOOP_FMT_D88      1
#define LOOP_FMT_FDI      2
#define LOOP_FMT_HDI      3
#define LOOP_FMT_RAW      4
#define LOOP_FMT_NHD      5

/* メディア種別定数 (DA/UA 上位ニブル) */
#define LOOP_MEDIA_2HD_1232  0x90
#define LOOP_MEDIA_2DD       0x70
#define LOOP_MEDIA_2HD_144   0x30
#define LOOP_MEDIA_HDD       0x80

/* ================================================================== */
/*  CHS コア API                                                       */
/* ================================================================== */

/* CHS セクタ書き込み (全フォーマット共通)
 * D88: loop_dev_seek_d88 → vfs_write
 * FDI/HDI/RAW: オフセット計算 → vfs_write
 * sect は 1-based (ATA/FDC 準拠)
 * 戻り値: 0=成功, -1=セクタ不在/エラー */
int loop_dev_write_chs(int slot, u16 cyl, u8 head, u8 sect,
                        const void *buf);

/* D88 専用: FDC エミュレーション用 CHS セクタ検索
 *
 * µPD765A の READ DATA コマンドをエミュレートする際に使用。
 * トラック選択 (trk_cyl/trk_head): SEEK で設定された物理位置
 * セクタID照合 (id_c/id_h/id_r): コマンドの論理 C/H/R
 *
 * Ys 等のコピープロテクションでは:
 *   SEEK cyl=1 → READ C=0, H=1, R=1
 * のように物理トラックと論理IDが意図的に異なる。
 *
 * 戻り値: セクタデータのファイルオフセット (0=未発見)
 * out_data_len: セクタデータ長 (128<<N)
 * out_spt: トラック内セクタ数 */
u32 loop_dev_seek_d88(int slot,
                      u8 trk_cyl, u8 trk_head,
                      u8 id_c, u8 id_h, u8 id_r,
                      u32 *out_data_len, u16 *out_spt);

/* D88 専用: 物理トラック上の index 番目のセクタ ID を返す (READ ID 用)
 *
 * ヘッドの下を通る順に並んだセクタ ID を 1 つずつ取り出す。
 * ゲストはこれを何度か呼んでフォーマット (セクタ長 N / 本数) を判定する。
 * 戻り値: 0=成功, -1=トラック/セクタ不在 */
int loop_dev_track_id_d88(int slot, u8 trk_cyl, u8 trk_head, u16 index,
                          u8 *out_c, u8 *out_h, u8 *out_r, u8 *out_n,
                          u16 *out_spt);

/* スロットの VFS fd を取得 (-1 = 未アタッチ) */
int loop_dev_get_fd(int slot);

/* フォーマット種別取得 (LOOP_FMT_*)
 * 戻り値: LOOP_FMT_*, 未アタッチの場合 LOOP_FMT_NONE */
int loop_dev_get_format(int slot);

/* HDD イメージ判定
 * 戻り値: 1=HDI/NHD, 0=それ以外 */
int loop_dev_is_hdd(int slot);

/* メディア種別取得 (LOOP_MEDIA_*)
 * ジオメトリから FDD/HDD の DA/UA 上位ニブルを返す */
int loop_dev_get_media(int slot);

/* セクタ長コード N 取得 (log2(bps/128))
 * 例: 512B→2, 1024B→3 */
u8  loop_dev_get_sec_n(int slot);

#endif /* LOOP_DEV_H */
