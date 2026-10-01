/* T2c: high shared text aliases, per-AS private data; original pool frames
 * remain supervisor in master. Outer format/generations are checked before
 * publication. GUI_PROTO_VERSION is additionally checked by the app stub. */
#ifndef __SHLIB_H
#define __SHLIB_H

#include "types.h"
#include "paging.h"

/* 起動時に 1 回だけ呼ぶ (VFS 初期化後・シェル起動前、memory_boot_init 済み)。
 * 戻り値: 0=ロード成功, -1=未ロード (ファイルが無い / 形式不正)。
 * -1 でも起動は続行してよい (GUI を使わないプログラムには影響しない)。 */
int shlib_init(void);

/* ライブラリが常駐しているか (1=常駐)。 */
int shlib_loaded(void);

/* shlib_init がライブラリを載せずに断った理由。GUI を CUI へ落とす案内を
 * 分けるのに使う (直し方が逆向きなので混ぜない)。
 *   SHLIB_REJECT_LAYOUT  … 世代 / KAPI 配置違い (OS32X ヘッダ v4、票
 *                          TASK_KAPI_DATA_FIELDS)。/sys を作り直して配備する
 *   SHLIB_REJECT_MIN_API … このカーネルより新しい KAPI を要求している。
 *                          カーネルを先に更新する (要求版は shlib_reject_min_api)
 * ファイルが無い・形式不正は NONE のまま (従来どおり静かに未ロード)。 */
#define SHLIB_REJECT_NONE     0
#define SHLIB_REJECT_LAYOUT   1
#define SHLIB_REJECT_MIN_API  2
int shlib_reject_reason(void);
u32 shlib_reject_min_api(void);

/* 常駐しているライブラリの版 (OS32ShlibHeader.version)。未ロードなら 0。 */
u32 shlib_version(void);

/* .text/.rodata の終端 (exclusive)。未ロードなら MEM_SHLIB_BASE。 */
u32 shlib_text_end(void);
/* B1: exact registered RO backing, not merely the shlib owner. */
int shlib_read_page(u32 va, u32 frame);

/* attach 1 回あたり pgalloc から取る .data/.bss の複製ページ数。未ロードなら 0。
 * exec が「3 領域の外で per-app に取るページ」を勘定するのに使う (K7)。
 * ここを通さずに exec 側で枚数を決め打ちすると、ライブラリの .data が
 * 太ったときに黙って 8MB 構成の起動が落ちる。 */
u32 shlib_data_pages(void);

/* ring3 アドレス空間にライブラリを張る (paging_addrspace_create の直後)。
 *   - .text/.rodata を read-only + USER で
 *   - .data/.bss は原本から複製した専用の物理ページを同じ仮想番地に
 * master CR3 で呼ぶ。未ロードなら0。途中失敗は全data写像を返して-1、
 * exec は起動を取り消す。text原本を返さない。 */
int shlib_addrspace_attach(struct addrspace *as);

/* attach で複製した .data/.bss ページを解放する。
 * paging_addrspace_destroy の **前** に呼ぶこと。未 attach なら何もしない。 */
void shlib_addrspace_detach(struct addrspace *as);

#endif /* __SHLIB_H */
