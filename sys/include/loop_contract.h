#ifndef LOOP_CONTRACT_H
#define LOOP_CONTRACT_H

#include "types.h"

#define LOOP_MAX_SLOTS 4

/* Numeric return codes retain each interface's existing contract. */
#define LOOP_ERR_FORMAT (-2)
#define LOOP_ERR_BUSY (-3)
#define LOOP_ERR_IO (-4)
#define LOOP_ERR_UNSUPPORTED (-2)
#define LOOP_ERR_WRITE_PROTECTED (-3)

/* ループバックデバイス初期化 (lo0..lo3 を dev_register) */
void loop_dev_init(void);

/* パスベースのアタッチ (拡張子でフォーマット判別)
 * 戻り値: 0=成功, -1=パス/スロット不正, -2=フォーマット不正,
 *         -3=スロット使用中, -4=I/Oエラー */
int  loop_dev_attach(const char *vfs_path, int slot);

/* スロットをデタッチ (owns_fd の場合のみ fd をクローズ) */
void loop_dev_detach(int slot);

/* スロット状態照会
 * 戻り値: 1=アタッチ中, 0=未使用
 * out_total: 総LBA数, out_bps: セクタサイズ(bytes) */
int  loop_dev_status(int slot, u32 *out_total, int *out_bps);

/* CHS セクタ読み出し (全フォーマット共通)
 * D88: トラックテーブル走査 → セクタヘッダ照合 → データ読み出し
 * FDI/HDI/RAW: オフセット計算 → vfs_read
 * sect は 1-based (ATA/FDC 準拠)
 * 戻り値: 0=成功, -1=セクタ不在/エラー */
int loop_dev_read_chs(int slot, u16 cyl, u8 head, u8 sect, void *buf);

/* LBA セクタ読み出し。lba+count が総 LBA を超えたらエラー。
 * 戻り値: 0=成功, -1=範囲外/未アタッチ/I-Oエラー, -2=D88 は非対応 */
int loop_dev_read_lba(int slot, u32 lba, u32 count, void *buf);

/* LBA セクタ書き込み。書き込み不可アタッチなら -3。
 * 戻り値: 0=成功, -1=範囲外等, -2=D88 非対応, -3=ライトプロテクト */
int loop_dev_write_lba(int slot, u32 lba, u32 count, const void *buf);

/* 書き込み可能アタッチか (loop_dev_attach が O_RDWR で開けたか)
 * 戻り値: 1=書き込み可, 0=読み取り専用または未アタッチ */
int loop_dev_is_writable(int slot);

/* ジオメトリ取得
 * 戻り値: 0=成功, -1=未アタッチ */
int loop_dev_get_geometry(int slot, u16 *cyls, u8 *heads, u8 *spt,
                          u16 *bps, u32 *total_lba);

/* LBA 書き込み可能な形式を O_RDWR でアタッチしているか。 */
int loop_dev_lba_writable(int slot);

#endif
