/* ======================================================================== */
/*  CON_SINK.H — console シンク (GUI モード中のカーネル出力のリング)        */
/*                                                                          */
/*  票: docs/archive/gui_v13/TASK_K6C_console.md (K 側 = カーネル + KAPI v46)  */
/*                                                                          */
/*  GUI モード中は gshell が全画面 GFX を握るのでテキスト面は見えない。      */
/*  そこへ書かれた出力を捨てずに 8KB のリングへレコードで積み、端末アプリ    */
/*  (外部、K6C-A) が con_sink_read() で吸って描く。                          */
/*                                                                          */
/*  ワイヤ形式・レコード型・容量は os32_kapi_shared.h の CON_SINK_* が       */
/*  正典 (端末アプリと共有するので Shared Layer に置く)。ここには            */
/*  カーネル内部だけが使う宣言を置く。                                       */
/* ======================================================================== */

#ifndef __CON_SINK_H
#define __CON_SINK_H

#include "types.h"

/* リング容量 (カーネル帯の静的配列、kmalloc しない)。票 §2-1。 */
#define CON_SINK_RING_SIZE   8192

/* 読み手が居ないことを表す所有者 ID。res_owner_get() は top-level で 0 を
 * 返すので、0 を「空き」にはできない (シェル自身が読み手になれなくなる)。 */
#define CON_SINK_NO_READER   (-1)

/* あふれで捨てたレコード数 (カーネルシンボル。KAPI にはしない — 票 §2-1)。 */
extern volatile u32 con_sink_drop_count;

/* --- 入退場 (console.c の console_text_gdc_stop / _start が呼ぶ) --------- */
void con_sink_enable(void);    /* GUI へ入る: 空で始める */
void con_sink_disable(void);   /* CUI へ戻る: 溜まっているものを捨てる */
int  con_sink_is_enabled(void);

/* --- 積む側 (console.c の入口から。割込み文脈からも呼ばれる) ------------ */
void con_sink_push_print(const char *buf, u32 len, u8 color);
void con_sink_push_clear(void);
void con_sink_push_cursor(int x, int y);
/* con_sink_push_exit: 子アプリが畳まれた (票 T7 E1)。積むのは exec.c の
 * exec_reclaim_owned だけ — 「読み手本人の退場では積まない」の判定も
 * そちら側 (所有を返す前でないと照合できないため)。 */
void con_sink_push_exit(int id);

/* --- 読む側 (KAPI v46 の実体) ------------------------------------------- */
/* con_sink_read: レコード境界で切って buf へ写す。戻り値 = 書いたバイト数
 *   (0 = 空)。読み手は 1 本だけで、最初に呼んだ所有者 (res_owner_get()) が
 *   持つ。別の所有者からは OS32_ERR_EXIST。cap < CON_SINK_REC_MAX または
 *   buf == NULL は OS32_ERR_INVAL。 */
i32 con_sink_read(void *buf, u32 cap);
/* con_sink_stat: 溜まっているバイト数と捨てた回数。所有権は要らない。 */
i32 con_sink_stat(u32 *pending, u32 *dropped);
/* con_sink_reader_get: いまの読み手の所有者 ID (居なければ
 * CON_SINK_NO_READER)。KAPI にはしない — カーネル内で「注入してよいのは
 * 読み手だけ」を照合する kernel/kbd_inject.c のためだけの公開 (票 K7 §5 R2)。
 * 権限の表を 2 つ持たないための唯一の口で、所有を**動かす**口ではない。 */
int con_sink_reader_get(void);

/* --- 回収 (exec_reclaim_owned から) -------------------------------------- */
void con_sink_owner_exit(int id);

/* --- 自己診断 (kernel/kselftest.c) --------------------------------------- */
/* リングの push/pop/レコード境界/あふれ/破棄を確かめる。ビット 0..n が
 * 落ちた項目 (0 = 全部通った)。呼んだ後のリングは空・無効に戻る。 */
u32 con_sink_selftest(void);

#endif /* __CON_SINK_H */
