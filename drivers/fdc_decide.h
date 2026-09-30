/* ======================================================================== */
/*  FDC_DECIDE.H — FDC の純粋な判定 (I/O もタイマも触らない)                */
/*                                                                          */
/*  drivers/fdc.c から「ST0 をどう読むか」だけを切り出したもの。            */
/*  ポートも tick_count も参照しないので、**ホストでそのまま試験できる**。  */
/*  切り出した理由は、実機でしか踏まない 2 つの分岐                         */
/*    (a) pending が無いときの SENSE INTERRUPT STATUS の 1 バイト応答       */
/*    (b) RECALIBRATE の EC (77 ステップでトラック 0 に届かなかった)        */
/*  がエミュレータでは再現せず、机上の判断のまま実機で落ちていたため。      */
/*                                                                          */
/*  試験: tools/tests/fdc_seek_host.c + tools/tests/test_fdc_seek.py        */
/*  記録: tools/tests/fdc_seek_tdd.md                                       */
/*  出典: µPD765A データシート (ST0 / SENSE INTERRUPT STATUS)               */
/* ======================================================================== */

#ifndef FDC_DECIDE_H
#define FDC_DECIDE_H

#include "types.h"

/* ======================================================================== */
/*  ST0 (µPD765A ステータスレジスタ 0)                                      */
/* ======================================================================== */
#define FDC_ST0_DS_MASK     0x03    /* bit1-0: ドライブ番号 */
#define FDC_ST0_HD          0x04    /* bit2:   ヘッド */
#define FDC_ST0_NR          0x08    /* bit3:   Not Ready */
#define FDC_ST0_EC          0x10    /* bit4:   Equipment Check */
#define FDC_ST0_SE          0x20    /* bit5:   Seek End */
#define FDC_ST0_IC_MASK     0xC0    /* bit7-6: Interrupt Code */
#define FDC_ST0_IC_NORMAL   0x00    /* 00b: 正常終了 */
#define FDC_ST0_IC_ABNORMAL 0x40    /* 01b: 異常終了 */
#define FDC_ST0_IC_INVALID  0x80    /* 10b: Invalid command */
#define FDC_ST0_IC_RDYCHG   0xC0    /* 11b: Ready 線が変化した */

/* pending の割り込みが無いときに SENSE INTERRUPT STATUS が返す ST0。
 * µPD765A は「出す割り込みが無い状態の SIS」を invalid command として扱い、
 * ST0 = 80h を **1 バイトだけ** 返す (PCN は続かない)。 */
#define FDC_ST0_NO_PENDING  FDC_ST0_IC_INVALID

/* SIS のリザルト長 (バイト)。pending 有りなら ST0 + PCN の 2 バイト。 */
#define FDC_SIS_LEN_INVALID 1
#define FDC_SIS_LEN_NORMAL  2

/* ======================================================================== */
/*  fdc_classify_seek_end() の戻り値                                        */
/* ======================================================================== */
#define FDC_SEEK_OK        0  /* SE=1 / EC=0 / (照合するなら) PCN 一致 — 完了 */
#define FDC_SEEK_RETRY_EC  1  /* SE=1 だが EC=1 — RECALIBRATE をもう一度出す */
#define FDC_SEEK_PENDING   2  /* ST0=80h — まだ終わっていない (pending 無し) */
#define FDC_SEEK_FAIL      3  /* それ以外 (Ready 変化 / PCN 不一致 など) */
#define FDC_SEEK_NOT_READY 4  /* NR=1 — 媒体もドライブも無い。**回復で直らない** */

/* ======================================================================== */
/*  判定                                                                    */
/* ======================================================================== */

/* SENSE INTERRUPT STATUS のリザルトを何バイト読むか (1 か 2)。
 *
 * **ここを 2 固定にすると実機でもエミュレータでも損をする。** pending が
 * 無いときの応答は ST0 だけの 1 バイトで、2 バイト目を待つと来ないバイトを
 * FDC_TIMEOUT_LOOP 回空転してから諦めることになる。排水ループは pending が
 * 尽きるまで回すので、**必ず毎回 1 度はこの空振りを踏む**。
 *
 * NP21/W も同じ 1 バイト応答を返す — np21w-src/src/io/fdc.c の
 * FDC_SenceintStatus() は 4 ドライブ分の fdc.stat[] が全部 0 のとき
 * `fdc.buf[0] = FDCRLT_IC1; fdc.bufcnt = 1;` とする (FDCRLT_IC1 = 0x80)。 */
int fdc_sis_result_bytes(u8 st0);

/* シーク (SEEK / RECALIBRATE) の完了を ST0 と PCN から判定する。
 *
 * NR (Not Ready) は `FDC_SEEK_NOT_READY` として他の失敗と分ける。
 * 媒体もドライブも無いのはリセットでも RECALIBRATE でも直らないので、
 * 呼び出し側は **リトライも回復もせずに即座に最終失敗にする**。
 * ここを一緒くたにしていたころは、HDD 起動時の /fd0 サブマウント試行が
 * 空のドライブに対して毎回 fdc_recover (リセット待ち + recalibrate) を
 * 3 回踏み、起動が数秒伸びていた。
 *
 *   want_cyl >= 0  … PCN が want_cyl と一致することまで求める (SEEK)
 *   want_cyl <  0  … PCN を照合しない (RECALIBRATE — 成功条件は
 *                    「SE が立ち、EC が立っていない」ことだけ)
 *
 * EC は「77 ステップ踏んでもトラック 0 のセンサが反応しなかった」印。
 * 80 シリンダ媒体でヘッドが 77 より奥に居ると正常な機械でも立つので、
 * 失敗ではなく **もう一度 RECALIBRATE を出す** 合図として返す。 */
int fdc_classify_seek_end(u8 st0, u8 pcn, int want_cyl);

/* まとめ読み (READ DATA で count セクタ) の IRQ 待ちの上限 (tick)。
 * 機構の最悪値から引く (fdc.h の FDC_ROT_TICKS_WORST の注記):
 *   FDC_TIMEOUT_MARGIN × (FDC_FIND_ROTATIONS 回転 + ceil(count / spt) 回転
 *                         + ヘッドロード)
 * ただし単発の上限 (floor_ticks) を下回らない。
 * spt == 0 や count < 1 のときは floor_ticks を返す。
 * 定数は呼び手が渡す — このファイルは fdc.h を引かない (I/O の定義を
 * ホストへ持ち込まないため)。 */
u32 fdc_rw_timeout_ticks(int spt, int count, u32 rot_ticks, u32 find_rot,
                         u32 head_load_ticks, u32 margin, u32 floor_ticks);

/* FD の受け皿 (1 本の静的な領域) の割り付け。
 *   領域 [start, start + nslot_plus_dma × slot) のうち、DMA に使う slot
 *   バイトの窓 1 つと、CPU だけが触る連続した先読み用の領域を決める。
 *   **DMA の窓は 64KB 境界をまたがない** ([HW2])。先読み用はまたいでよい。
 *   領域は 64KB より短いので境界は高々 1 本で、窓は先頭か末尾のどちらかに
 *   必ず取れる (境界が先頭 slot バイトの中にあれば末尾に置く)。
 *   *dma_off / *rest_off は start からのオフセット。先読み用は
 *   (total - slot) バイトの連続領域。
 *   0 = 取れた / -1 = 取れない (total < slot、total >= 64KB、slot == 0)。
 * 揃え指定に頼らないので .bss の詰め物が出ない (2026-09-24 のレビュー)。 */
int fdc_buf_layout(u32 start, u32 total, u32 slot, u32 *dma_off, u32 *rest_off);

#endif /* FDC_DECIDE_H */
