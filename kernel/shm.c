/* ======================================================================== */
/*  SHM.C — 共有メモリ管理 (IPC用)                                          */
/*                                                                          */
/*  カーネル帯域 (0x100000-0x1FFFFF) 内の KAPI テーブル直後に                 */
/*  256KB の共有メモリ空間を配置。                                            */
/*  16KB × 16ブロックの固定長ブロック分割管理。                              */
/*                                                                          */
/*  ガードページ:                                                           */
/*    MEM_SHM_GUARD_LO (前方, NOT PRESENT) — オーバーフロー保護              */
/*    MEM_SHM_GUARD_HI (後方, NOT PRESENT) — オーバーフロー保護              */
/*                                                                          */
/*  ページ保護:                                                             */
/*    shm_lock()  — ブロックの全ページを Read-Only に変更                   */
/*    shm_free()  — ブロックの全ページを Read-Write に戻す                  */
/* ======================================================================== */

#include "shm.h"
#include "paging.h"
#include "kstring.h"

/* shm.h のブロック定義と memmap.h の帯域サイズは別々に書かれている。
 * 食い違うとガードページの位置やゼロクリア範囲が実帯域からずれる。 */
STATIC_ASSERT(SHM_TOTAL_SIZE == MEM_SHM_SIZE, shm_size_matches_memmap);

/* GUI 予約 (契約 T2): SHM 帯の **末尾 4 ブロック**。
 * 先頭とサイズがブロック境界に乗っていること、SHM 帯に収まることを固定する。
 *
 * 2026-09-17 (決裁 D1): 下の 2 本は 2026-09-17 まで **黙って死んでいた**。
 * 条件に MEM_SHM_GUI_BASE / MEM_SHM_BASE (= (u32)&__bss_end 由来) が入って
 * いたため C の整数定数式にならず、GCC はファイルスコープの可変長配列として
 * 警告だけ出して通していた (本番ビルドのログに毎回出ていた)。
 * MEM_SHM_GUI_OFFSET (= MEM_SHM_SIZE - MEM_SHM_GUI_SIZE、純粋な定数式) で
 * 書き直して生き返らせた。**条件に浮動番地を混ぜないこと。** */
#define SHM_GUI_BLOCK_FIRST  ((int)(MEM_SHM_GUI_OFFSET / SHM_BLOCK_SIZE))
#define SHM_GUI_BLOCK_COUNT  ((int)(MEM_SHM_GUI_SIZE / SHM_BLOCK_SIZE))
STATIC_ASSERT(MEM_SHM_GUI_OFFSET % SHM_BLOCK_SIZE == 0, shm_gui_base_aligned);
STATIC_ASSERT(MEM_SHM_GUI_SIZE % SHM_BLOCK_SIZE == 0, shm_gui_size_aligned);
STATIC_ASSERT(GUI_SLOT_SIZE == SHM_BLOCK_SIZE, shm_gui_slot_is_block);
STATIC_ASSERT(SHM_GUI_BLOCK_COUNT == GUI_SLOT_MAX, shm_gui_slot_count);
STATIC_ASSERT(MEM_SHM_GUI_OFFSET + MEM_SHM_GUI_SIZE <= SHM_TOTAL_SIZE,
              shm_gui_within_band);
/* 予約が帯の **末尾** にあること (先頭からの決め打ちに戻したら落ちる) */
STATIC_ASSERT(MEM_SHM_GUI_OFFSET + MEM_SHM_GUI_SIZE == MEM_SHM_SIZE,
              shm_gui_is_last_blocks);

/* ブロック管理テーブル */
static u8 shm_state[SHM_BLOCK_COUNT]; /* 各ブロックの状態 */
static int shm_block_span[SHM_BLOCK_COUNT]; /* 各確保の先頭ブロックが持つスパン数 */
/* 確保した所有者 (res_owner_get() の値 = アプリ ID)。0 = 所有者なし。
 * 票 K5 の D3/P3: これが無いと exec_exit がアプリ A の終了で B のブロックまで
 * 解放してしまう (アプリ 4 本同時では実際に起きる)。 */
static int shm_block_owner[SHM_BLOCK_COUNT];

/* res_owner_get() は fs/fd_redirect.c。kernel/ は -Ifs を持たないので
 * kernel/gui.c と同じ流儀で extern 宣言する。 */
extern int res_owner_get(void);

/* ブロックインデックス → 物理/仮想アドレス変換 */
static u32 block_to_addr(int idx)
{
    return MEM_SHM_BASE + (u32)idx * SHM_BLOCK_SIZE;
}

/* アドレス → ブロックインデックス変換 (-1=無効) */
static int addr_to_block(void *ptr)
{
    u32 addr = (u32)ptr;
    int idx;
    if (addr < MEM_SHM_BASE || addr >= MEM_SHM_BASE + SHM_TOTAL_SIZE) {
        return -1;
    }
    idx = (int)((addr - MEM_SHM_BASE) / SHM_BLOCK_SIZE);
    /* ブロック境界チェック */
    if (addr != block_to_addr(idx)) {
        return -1;
    }
    return idx;
}

/* ======================================================================== */
/*  shm_init — 共有メモリ初期化                                             */
/*  ガードページ設定 + 共有メモリ領域をR/Wに設定                            */
/* ======================================================================== */
void shm_init(void)
{
    int i;

    /* 全ブロックを未使用に初期化 */
    for (i = 0; i < SHM_BLOCK_COUNT; i++) {
        shm_state[i] = SHM_FREE;
        shm_block_span[i] = 0;
        shm_block_owner[i] = 0;
    }

    /* GUI 予約 (契約 T2): ブロック 12〜15 を固定予約。shm_alloc は SHM_FREE
     * だけを配るので予約済みは配られず、shm_cleanup_all / shm_free も
     * SHM_RESERVED を触らない。span は 0 のまま (単独管理ではない)。 */
    for (i = SHM_GUI_BLOCK_FIRST;
         i < SHM_GUI_BLOCK_FIRST + SHM_GUI_BLOCK_COUNT; i++) {
        shm_state[i] = SHM_RESERVED;
    }

    /* ガードページ設定 */
    paging_set_not_present(MEM_SHM_GUARD_LO,
                           MEM_SHM_GUARD_LO + PAGE_SIZE - 1);
    paging_set_not_present(MEM_SHM_GUARD_HI,
                           MEM_SHM_GUARD_HI + PAGE_SIZE - 1);

    /* 共有メモリ領域を R/W に設定 (アイデンティティマッピング) */
    paging_map_range(MEM_SHM_BASE, MEM_SHM_BASE + SHM_TOTAL_SIZE,
                     MEM_SHM_BASE, PAGE_RW);
}

/* ======================================================================== */
/*  shm_alloc — 連続ブロック確保 (ファーストフィット)                        */
/*  戻り値: 先頭アドレス, NULL=空き不足                                     */
/* ======================================================================== */
void *shm_alloc(int block_count)
{
    int start, i, found;

    if (block_count <= 0 || block_count > SHM_BLOCK_COUNT) {
        return (void *)0;
    }

    /* ファーストフィット探索 */
    for (start = 0; start <= SHM_BLOCK_COUNT - block_count; start++) {
        found = 1;
        for (i = 0; i < block_count; i++) {
            if (shm_state[start + i] != SHM_FREE) {
                found = 0;
                start += i; /* 次の候補へスキップ */
                break;
            }
        }
        if (found) {
            /* 確保 */
            for (i = 0; i < block_count; i++) {
                shm_state[start + i] = SHM_USED;
                shm_block_owner[start + i] = res_owner_get();
            }
            shm_block_span[start] = block_count;
            /* 前のプログラムの IPC データを次のプログラムに
             * 見せないよう、確保時に必ずゼロクリアする */
            kmemset((void *)block_to_addr(start), 0,
                    (u32)block_count * SHM_BLOCK_SIZE);
            return (void *)block_to_addr(start);
        }
    }

    return (void *)0; /* 空き不足 */
}

/* ======================================================================== */
/*  shm_lock — ブロックを Read-Only に保護                                  */
/*  戻り値: 0=成功, -1=不正なポインタ                                       */
/* ======================================================================== */
int shm_lock(void *ptr)
{
    int idx, span, i;
    u32 addr;

    idx = addr_to_block(ptr);
    if (idx < 0) return -1;

    span = shm_block_span[idx];
    /* span が壊れていると idx+i が管理テーブル外に出る */
    if (span <= 0 || idx + span > SHM_BLOCK_COUNT) return -1;

    for (i = 0; i < span; i++) {
        if (shm_state[idx + i] != SHM_USED) return -1;
    }

    /* 全ブロックの全ページを Read-Only に (連続範囲なので一括) */
    addr = block_to_addr(idx);
    paging_map_range(addr, addr + (u32)span * SHM_BLOCK_SIZE, addr, PAGE_RO);
    for (i = 0; i < span; i++) {
        shm_state[idx + i] = SHM_LOCKED;
    }

    return 0;
}

/* ======================================================================== */
/*  shm_free — ブロックを解放 (R/W に戻す)                                  */
/*  戻り値: 0=成功, -1=不正なポインタ                                       */
/* ======================================================================== */
int shm_free(void *ptr)
{
    int idx, span, i;
    u32 addr;

    idx = addr_to_block(ptr);
    if (idx < 0) return -1;

    span = shm_block_span[idx];
    /* span が壊れていると idx+i が管理テーブル外に出る */
    if (span <= 0 || idx + span > SHM_BLOCK_COUNT) return -1;

    /* 全ブロックの全ページを R/W に戻して解放 (連続範囲なので一括) */
    addr = block_to_addr(idx);
    paging_map_range(addr, addr + (u32)span * SHM_BLOCK_SIZE, addr, PAGE_RW);
    for (i = 0; i < span; i++) {
        shm_state[idx + i] = SHM_FREE;
        shm_block_owner[idx + i] = 0;
    }
    shm_block_span[idx] = 0;

    return 0;
}

/* ======================================================================== */
/*  shm_free_owned — 指定所有者のブロックだけ解放 (票 K5 の D3、P3)          */
/*                                                                          */
/*  exec_exit / exec_kill がアプリ ID で呼ぶ。**その ID が確保したものだけ** */
/*  を返すので、同時に生きている他のアプリのブロックは 1 つも動かない。      */
/*  GUI 予約 (SHM_RESERVED) は所有者 0 のまま触らない (契約 T2)。            */
/* ======================================================================== */
void shm_free_owned(int owner)
{
    int i;
    u32 blk_start;

    if (owner == 0) return;             /* 所有者なしのタグは回収対象外 */
    for (i = 0; i < SHM_BLOCK_COUNT; i++) {
        if (shm_state[i] == SHM_RESERVED) continue;
        if (shm_state[i] == SHM_FREE) continue;
        if (shm_block_owner[i] != owner) continue;
        /* ページ属性を R/W に戻す (lock されていたぶんを含む) */
        blk_start = block_to_addr(i);
        paging_map_range(blk_start, blk_start + SHM_BLOCK_SIZE,
                         blk_start, PAGE_RW);
        shm_state[i] = SHM_FREE;
        shm_block_owner[i] = 0;
        shm_block_span[i] = 0;
    }
}

/* ======================================================================== */
/*  shm_cleanup_all — 全ブロックを強制解放 (プログラム終了時の安全網)        */
/*  プログラムがshm_free()を呼び忘れても、exec_exit()で自動回収する。        */
/* ======================================================================== */
void shm_cleanup_all(void)
{
    int i;
    u32 blk_start;

    for (i = 0; i < SHM_BLOCK_COUNT; i++) {
        /* GUI 予約ブロックはプログラム終了で回収しない (契約 T2)。 */
        if (shm_state[i] == SHM_RESERVED) {
            continue;
        }
        if (shm_state[i] != SHM_FREE) {
            /* ページ属性をR/Wに戻す */
            blk_start = block_to_addr(i);
            paging_map_range(blk_start, blk_start + SHM_BLOCK_SIZE,
                             blk_start, PAGE_RW);
            shm_state[i] = SHM_FREE;
        }
        shm_block_owner[i] = 0;
        shm_block_span[i] = 0;
    }
}
