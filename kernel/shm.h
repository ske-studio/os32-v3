/* ======================================================================== */
/*  SHM.H — 共有メモリ管理 (IPC用)                                          */
/*                                                                          */
/*  プロセス切り替え下でも全プログラムから参照可能な静的共有メモリ帯域。       */
/*  カーネル予約域 0x200000 付近に 256KB を配置する。                         */
/*  16KBブロック × 16個の固定長ブロック分割管理。                            */
/*  ページ保護によるロック (R/O) / アンロック (R/W) をサポート。              */
/* ======================================================================== */

#ifndef __SHM_H
#define __SHM_H

#include "types.h"
#include "memmap.h"

/* ブロック設定 */
#define SHM_BLOCK_SIZE   (16 * 1024)   /* 16KB */
#define SHM_BLOCK_COUNT  14            /* 14ブロック (2026-09-17 決裁 D1: 16 → 14) */
#define SHM_TOTAL_SIZE   (SHM_BLOCK_SIZE * SHM_BLOCK_COUNT) /* 224KB */

/* ブロック状態 */
#define SHM_FREE     0   /* 未使用 */
#define SHM_USED     1   /* 確保済み (R/W) */
#define SHM_LOCKED   2   /* ロック済み (R/O) */
#define SHM_RESERVED 3   /* カーネルが固定予約 (GUI ブロック 12〜15 等)。
                          * shm_alloc は配らず、shm_free / shm_cleanup_all は
                          * 触らない (契約 T2)。 */

/* ======== API ======== */

/* 初期化 (paging_init の後に呼ぶ) */
void shm_init(void);

/* ブロック確保 (連続 block_count ブロック)
 * 戻り値: 先頭アドレス, NULL=空き不足 */
void *shm_alloc(int block_count);

/* ブロックをRead-Onlyに保護 (ロック)
 * 戻り値: 0=成功, -1=不正なポインタ */
int shm_lock(void *ptr);

/* ブロックを解放 (R/Wに戻す)
 * 戻り値: 0=成功, -1=不正なポインタ */
int shm_free(void *ptr);

/* 全ブロックを強制解放 (プログラム終了時のクリーンアップ用)。
 * **所有者を見ない**ので、アプリが 4 本同時に生きる v1.3 では使わない
 * (アプリ A の終了がアプリ B のブロックを巻き上げる)。ブート時の初期化と
 * 診断のために残す。 */
void shm_cleanup_all(void);

/* 指定所有者 (確保時の res_owner_get() の値でタグ付け) のブロックだけ解放。
 * exec_exit / exec_kill が ID 単位で呼ぶ (票 K5 の D3、P3)。
 * GUI 予約ブロック (SHM_RESERVED) は触らない (契約 T2)。 */
void shm_free_owned(int owner);

#endif /* __SHM_H */
