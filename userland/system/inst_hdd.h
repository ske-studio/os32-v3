/* ======================================================================== */
/*  INST_HDD.H — インストーラの hd0 の検査と書き込みの手順 (cdinst / install) */
/*                                                                          */
/*  票 docs/archive/realhw_v21/TASK_HDD_INSTALL.md 段 2 / §1-v3 N4・N6・R3-1。     */
/*  順序 (R3-1、ユーザー決裁 B、ERASE の位置は 2026-09-25 の PM 決定):       */
/*                                                                          */
/*   検査 (1 セクタも書かない)                                               */
/*     inst_hdd_check        幾何 (BIOS / ATA)・計画・LBA 0/1 のモード・     */
/*                           マウント (ルートの hd0 は断る)。表が使えない   */
/*                           (他の OS の区画など) ときは要約を出し、消した   */
/*                           後の空のディスクとみなして先へ進む             */
/*                           (InstTarget.erase_needed)                      */
/*     inst_hdd_check_media  IPL / ローダ / vmkernel.lz4 の大きさ・容量      */
/*     inst_hdd_describe     確認画面 (作り直すと一時置き場のデータが消える、 */
/*                           消す必要があれば「y の後に ERASE」)            */
/*   呼び手が y/N を取る (inst_hdd_getkey)                                   */
/*     inst_hdd_ask_erase    erase_needed なら ERASE の打鍵を求める。ERASE  */
/*                           以外なら負 (まだ 1 セクタも書いていない)       */
/*   書く                                                                    */
/*     inst_hdd_release      hd0 のマウントを umount_checked で外す (N6)。   */
/*                           外れない・まだマウントが残るなら断る (消去も   */
/*                           format もしない。ただし umount 自身が外す前に  */
/*                           ext2 を sync するので、umount を呼んだ後の断り */
/*                           は「何も書いていない」とは言わない)。          */
/*                           erase_needed なら検査の通った後で LBA 0/1 を 0 */
/*                           で埋めて読み戻す (インストーラ自身の最初の書き */
/*                           込み)                                          */
/*     inst_hdd_prepare      ext2_format_at → 区画表 → 読み戻し比較 →        */
/*                           通常のマウント (/hd0)                           */
/*     inst_hdd_write_boot   ローダ (LBA 2〜) → IPL (LBA 0、BIOS 幾何) を     */
/*                           それぞれ読み戻して比較                          */
/*   この後で呼び手が展開 → sync。                                           */
/*                                                                          */
/*  N4 の例外 (明示の消去、2026-09-25 ユーザー指示): inst_classify が他の    */
/*  区画・2 項目以上・55AA・開始違い・壊れた項目で断ったときだけ、要約を出し */
/*  て「消した後の空のディスク」として全検査 → 確認画面 → y/N まで進み、     */
/*  y の後で ERASE (大文字 5 文字、完全一致) の打鍵を求める。受けたら (/hd0  */
/*  のマウントを外し) LBA 0 と 1 をゼロで埋めて読み戻し、空のディスクとして  */
/*  同じ手順に進む。それ以外の入力は 1 セクタも書かずに終える (umount の  */
/*  前)。y の後の最初の行末 1 つは y の行末として読み捨てる (端末の         */
/*  y + Enter が ERASE の空行にならないように)。消すのは全検査               */
/*  と y/N の**後**なので、大きさ・容量・マウントで断る事態は消す前に分かる。 */
/*  消した後の失敗は INCOMPLETE と出し、「区画表は消した、もう一度実行すれば  */
/*  空のディスクとして入れられる」と案内する (InstTarget.erased)。            */
/*                                                                          */
/*  鍵の読み方 (inst_hdd_getkey): kbd / serial の trygetchar は「入力なし」  */
/*  を -1、受けた NUL を 0 で返すので、0 以上はすべて入力として扱う (NUL を  */
/*  読み捨てると ERA<NUL>SE<CR> で消えてしまう)。CR の直後の LF は 1 つの    */
/*  行末の一部として捨て、次の問いに持ち越さない (CRLF)。                     */
/*                                                                          */
/*  書いた後の失敗は "INCOMPLETE" と出して負を返す。umount の前の失敗は     */
/*  "Nothing was written"、umount を呼んだ後の断りは "Nothing was erased or  */
/*  formatted" (umount の sync の書き出しはあり得る) と出す。                */
/*  どちらの状態でも次の実行は通る: format の失敗なら区画表はまだ空 (空の    */
/*  ディスク)、区画表を書いた後なら OS32 の項目                               */
/*  1 つ (再作成) — ローダと IPL はマウントの確認の**後**に書くので、途中で   */
/*  止まっても LBA 0 の 55AA だけが残って「空でない」になることは無い。       */
/* ======================================================================== */

#ifndef INST_HDD_H
#define INST_HDD_H

#include "os32api.h"
#include "inst_disk.h"

#define INST_DRIVE        0          /* hd0 = IDE 0 = DA 80h だけ */
#define INST_DEV          "hd0"
#define INST_MOUNT        "/hd0"

typedef struct {
    HddGeom    hg;
    HdprepGeom g;
    HdprepPlan plan;
    int        mode;          /* INST_MODE_* (erase_needed なら INST_MODE_EMPTY) */
    int        mounts;        /* 検査の時点の dev_mount_count(0) */
    int        umount_hd0;    /* hd0 は /hd0 に 1 つだけマウントされている (外す) */
    int        erase_needed;  /* 表が使えない: y の後に ERASE を受けてから消す */
    int        erase_code;    /* erase_needed の理由 (inst_classify の負の値) */
    int        erased;        /* この実行で ERASE を受けて LBA 0/1 をゼロにした */
} InstTarget;

/* 消去の打鍵 (大文字 5 文字、完全一致。前後の空白も許さない) */
#define INST_ERASE_WORD   "ERASE"
#define INST_LINE_MAX     16

/* 1 鍵を待つ (kbd → serial)。0 以上 (NUL を含む) を返す。CR の直後の LF は捨てる */
int  inst_hdd_getkey(KernelAPI *api);
/* 1 字の答えの直後の問い用: 最初の 1 字が行末なら 1 回だけ捨てる (前の答えの
 * Enter)。2 つめの行末・NUL・ESC・字はそのまま返す */
int  inst_hdd_getkey_after_key(KernelAPI *api);

int  inst_hdd_check(KernelAPI *api, InstTarget *t);
int  inst_hdd_check_media(KernelAPI *api, const InstTarget *t,
                          u32 ipl_len, u32 loader_len, u32 kernel_len,
                          const InstNeed *need);
void inst_hdd_describe(KernelAPI *api, const InstTarget *t);
/* y の後: erase_needed なら ERASE の打鍵を求める。0 = 受けた (か不要)。
 * それ以外の入力なら表示して負 (何も書いていない)。y の後の最初の行末 1 つは
 * y の行末として読み捨てる (もう届いていても後から届いても) */
int  inst_hdd_ask_erase(KernelAPI *api, const InstTarget *t);
/* hd0 のマウントを外す (N6)。失敗・外れないなら負 (消去・format はしない。
 * umount を呼んだ後なら umount の sync による書き出しはあり得る)。
 * erase_needed ならその後で LBA 0/1 を 0 にして読み戻す (失敗は INCOMPLETE) */
int  inst_hdd_release(KernelAPI *api, InstTarget *t);
/* ext2_format_at → 区画表 → 読み戻し → マウント。失敗は INCOMPLETE */
int  inst_hdd_prepare(KernelAPI *api, const InstTarget *t);
int  inst_hdd_write_boot(KernelAPI *api, const InstTarget *t,
                         const u8 *ipl, u32 ipl_len,
                         const u8 *loader, u32 loader_len);
/* 書いた後の失敗の表示 (展開・sync の失敗にも呼び手が使う) */
void inst_hdd_incomplete(KernelAPI *api, const char *what, int rc);

#endif /* INST_HDD_H */
