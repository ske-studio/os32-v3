/* ======================================================================== */
/*  IME.C — OS32 カーネルFEP メインロジック                                  */
/*                                                                          */
/*  IME状態管理・キー処理・プリエディット表示・公開API                        */
/*  ローマ字変換は ime_romkana.c、辞書検索は ime_dict.c に分離               */
/* ======================================================================== */

#define IME_DICT_LARGE      2
#define IME_CAND_COLS       3
#define IME_CAND_COL_WIDTH  26
#define IME_CAND_LABEL_COLS 2
#define IME_PAGE_COL        74
/* "dd/dd)" after the opening parenthesis. */
#define IME_COUNT_TEXT_COLS ((int)sizeof("00/00)") - 1)
#include "memmap.h"
#include "ime.h"
#include "../sqlite_engine.h"
#include "../../drivers/input/kbd.h"
#include "tvram.h"
#include "console.h"
#include "utf8.h"
#include "kprintf.h"
#include "kstring.h"
#include "os32_kapi_shared.h"
#include "../../mm/kmalloc.h"
#include "exec.h"
#include "con_sink.h"
#include "fd_redirect.h"
#include "redir_access.h"
#include "io.h"
extern int ring3_call_from_user(void);
extern void ring3_abort_check(void);

#ifdef OS32_FEP_MODULE
#include "fep_imports.h"
#define services_public fep_services_public()
#define services_boot_fault fep_services_boot_fault()
#define kbd_shift_state fep_kbd_shift_state()
#endif


/* ======================================================================== */
/*  グローバル IME 状態                                                      */
/* ======================================================================== */

static IME_State g_ime;
static int g_ime_reader_owner;
static int g_ime_cui_redraw;
static void ime_cancel_input(void);

#include "ime_render.inc"

#include "ime_convert.inc"
#include "ime_delivery.inc"

/* ======================================================================== */
/*  公開API                                                                  */
/* ======================================================================== */

void ime_init(void)
{
    kmemset(&g_ime, 0, sizeof(IME_State));
    ime_delivery_clear();
    g_ime.mode = IME_MODE_OFF;
    g_ime.dict_loaded = 0;
    g_ime.state = IME_ST_INPUT;
    g_ime.per_page = IME_CAND_COLS * CANDLIST_SAVE_ROWS;
    g_ime.render = &g_ime_render_tvram;
    ime_rk_init(&g_ime.rk);

}

/* Dynamic FEP resources remain private until the shared service gate opens. */
u32 ime_boot_bytes(void)
{
    return (g_cand_save_text ? kmalloc_block_size(g_cand_save_text) + BLK_HDR_SIZE : 0) +
           (g_cand_save_attr ? kmalloc_block_size(g_cand_save_attr) + BLK_HDR_SIZE : 0);
}

int ime_dict_boot(void)
{
    if (g_ime.dict_loaded) return 0;
    if (services_boot_fault != SERVICES_FAULT_FEP_TEXT)
        g_cand_save_text = kmalloc(CANDLIST_SAVE_ROWS * TVRAM_COLS * sizeof(u16));
    if (!g_cand_save_text) return -1;
    if (services_boot_fault != SERVICES_FAULT_FEP_ATTR)
        g_cand_save_attr = kmalloc(CANDLIST_SAVE_ROWS * TVRAM_COLS * sizeof(u8));
    if (!g_cand_save_attr) return -1;
    if (services_boot_fault == SERVICES_FAULT_FEP_CRC ||
        services_boot_fault == SERVICES_FAULT_DICTIONARY) return -1;
    if (ime_dict_open(&g_ime.dict, IME_DICT_PATH) != 0) return -1;
    if (!g_ime.dict.learn_stmt)
        kprintf(ATTR_RED, "IME: 学習不可 (learning unavailable), conversion enabled\r\n");
    g_ime.dict_loaded = 1;
    return 0;
}

int ime_boot_abort(void)
{
    g_ime.mode = IME_MODE_OFF;
    if (ime_dict_close(&g_ime.dict)) return -1;
    g_ime.dict_loaded = 0;
    if (g_cand_save_attr) kfree(g_cand_save_attr);
    if (g_cand_save_text) kfree(g_cand_save_text);
    g_cand_save_attr = 0;
    g_cand_save_text = 0;
    return 0;
}

void ime_toggle(void)
{
    if (!services_public) return;
    if (g_ime.mode == IME_MODE_OFF) {
        if (!g_ime.dict_loaded) {
            if (ime_dict_open(&g_ime.dict, IME_DICT_PATH) == 0) {
                g_ime.dict_loaded = 1;
            } else {
                kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
                return;
            }
        }
        g_ime.mode = IME_MODE_HIRAGANA;
        g_ime.state = IME_ST_INPUT;
        g_ime.kana_buf[0] = '\0';
        g_ime.kana_len = 0;
        g_ime.converting = 0;
        ime_rk_init(&g_ime.rk);
        tvram_set_scroll_reserve(1);   /* 25 行目を保護 */
        preedit_draw();
    } else {
        /* かなバッファに残りがあれば確定 */
        if (g_ime.kana_len > 0 || g_ime.rk.preedit[0] != '\0') {
            commit_kana_direct();
        }
        g_ime.mode = IME_MODE_OFF;
        g_ime.converting = 0;
        g_ime.state = IME_ST_INPUT;
        tvram_set_scroll_reserve(0);   /* 保護解除 */
        candlist_clear();              /* 候補リスト領域を消去 */
        preedit_clear();
    }
}

int ime_is_active(void)
{
    if (!services_public) return 0;
    return g_ime.mode != IME_MODE_OFF;
}

void ime_set_mode(int mode)
{
    if (!services_public) return;
    if (mode < IME_MODE_OFF || mode > IME_MODE_KATAKANA) return;

    /* FEP ON 時は辞書を遅延オープンする (ime_toggle と同じ)。
     * これを欠くと dict->db が NULL のまま ime_dict_search が常に
     * 0 件を返し、変換がかな確定にフォールバックする。 */
    if (mode != IME_MODE_OFF && !g_ime.dict_loaded) {
        if (ime_dict_open(&g_ime.dict, IME_DICT_PATH) == 0) {
            g_ime.dict_loaded = 1;
        } else {
            kprintf(ATTR_RED, "IME: Dict load failed, FEP disabled\r\n");
            return;
        }
    }
    g_ime.mode = mode;
    if (mode != IME_MODE_OFF) {
        candlist_clear(); /* Restore the list before changing input mode. */
        g_ime.state = IME_ST_INPUT;
        g_ime.converting = 0;
        tvram_set_scroll_reserve(1);
        preedit_draw();
    } else {
        tvram_set_scroll_reserve(0);
        candlist_clear();              /* 候補リスト領域を消去 */
        preedit_clear();
    }
}

int ime_get_mode(void)
{
    if (!services_public) return IME_MODE_OFF;
    return g_ime.mode;
}

/* Shell reload clears TVRAM after gui_owner_exit. Restore the shared mode
 * when CUI starts waiting, before consuming its first key. */
static void ime_prepare_cui_input(void)
{
    if (!g_ime_cui_redraw) return;
    g_ime_cui_redraw = 0;
    if (ime_is_active()) preedit_draw();
}

static int ime_getchar_wait(int interruptible)
{
    int keydata;
    int result;

    /* WM already passed GUI text through FEP before terminal injection. */
    if (con_sink_is_enabled()) return kbd_getkey_wait(interruptible);
    ime_prepare_cui_input();

    for (;;) {
        keydata = ime_delivery_take();
        if (keydata >= 0) {
            u8 a = (u8)(keydata & 0xFF);
            if (a != 0) return (int)a;
            continue;
        }

        keydata = kbd_getkey_wait(interruptible);
        if (keydata < 0) return -1;

        /* Shift+Space: ON/OFF問わず常に検出 */
        if (((keydata >> 8) & SCANCODE_KEY) == KEY_SPACE &&
            (kbd_shift_state & SHIFT_SHIFT)) {
            ime_toggle();
            if (g_ime.commit_pos < g_ime.commit_len) {
                return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
            }
            continue;
        }

        /* FEP OFF: ASCII値をそのまま返す */
        if (g_ime.mode == IME_MODE_OFF) {
            u8 a = (u8)(keydata & 0xFF);
            if (a != 0) return (int)a;
            continue;
        }

        /* FEP ON: IME処理 */
        result = ime_delivery_process(keydata);
        if (result >= 0) {
            u8 a = (u8)(result & 0xFF);
            if (a != 0) return (int)a;
        }
    }
}

/* Only an actual CUI read takes custody of the shared composition. An empty
 * child poll must not make its exit cancel its parent's preedit. */
static void ime_note_cui_reader(void)
{
    if (ring3_call_from_user())
        g_ime_reader_owner = res_owner_get(); /* nonblocking CUI input */
}

int ime_trygetchar(void)
{
    int keydata;
    int result;

    if (con_sink_is_enabled()) return kbd_trygetchar();
    ime_prepare_cui_input();

    keydata = ime_delivery_take();
    if (keydata >= 0) {
        ime_note_cui_reader();
        u8 a = (u8)(keydata & 0xFF);
        return a != 0 ? (int)a : -1;
    }

    keydata = kbd_trygetkey();
    if (keydata < 0) return -1;
    ime_note_cui_reader();

    if (((keydata >> 8) & SCANCODE_KEY) == KEY_SPACE &&
        (kbd_shift_state & SHIFT_SHIFT)) {
        ime_toggle();
        if (g_ime.commit_pos < g_ime.commit_len) {
            return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
        }
        return -1;
    }

    if (g_ime.mode == IME_MODE_OFF) {
        u8 a = (u8)(keydata & 0xFF);
        if (a != 0) return (int)a;
        return -1;
    }

    result = ime_delivery_process(keydata);
    if (result >= 0) {
        u8 a = (u8)(result & 0xFF);
        if (a != 0) return (int)a;
    }
    return -1;
}

/* ======================================================================== */
/*  ime_feed_key — WM (gshell) が打鍵を 1 件 FEP に通す (GUI v1.1 W2、K 依頼 1)  */
/*                                                                          */
/*  GUI 中はカーネルが cooked キューに積まない (K2) ので、ime_trygetkey 系は  */
/*  キーを引けない。WM が raw から作った keydata = (scancode << 8) | ascii を  */
/*  ここへ渡す。keydata < 0 は「新しいキーは無い、確定文字列の続きだけ」。      */
/*  戻り値:                                                                 */
/*    < 0            FEP が消費した (WM はアプリへ Key を配送しない)          */
/*    >= 0x100       素通り (keydata そのもの)                               */
/*    == 0x1B        素通りの ESC (scancode 0 は ESC だけで ascii は 0x1B)     */
/*    1..0xFF        確定文字列の 1 バイト (UTF-8)。続きは keydata < 0 で引く  */
/*  分類が一意なのは、確定文字列 (UTF-8) に 0x1B が現れないため。            */
/*  SHIFT+SPACE の on/off は WM が ime_toggle で行うので、ここでは見ない。      */
/* ======================================================================== */
int ime_feed_key(int keydata)
{
    if (!services_public) return keydata;
    int result;

    /* WM input-target lifetime boundary: STOP cancels even an unread tail.
     * No dictionary access/learning, so this is also safe in X1. */
    if (keydata >= 0 && ((keydata >> 8) & SCANCODE_KEY) == KEY_STOP) {
        ime_cancel_input();
        return -1;
    }

    /* 確定済みの続きがあれば先にそれを返す */
    if (g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }
    if (keydata < 0) {
        return -1;
    }

    /* FEP OFF: 素通り */
    if (g_ime.mode == IME_MODE_OFF) {
        return keydata;
    }

    result = ime_process_key(keydata);
    if (result == 1 && g_ime.commit_pos < g_ime.commit_len) {
        return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
    }
    if (result == -1) {
        return keydata;      /* FEP が関与しないキー: 素通り */
    }
    return -1;               /* 消費 (未確定の更新など) */
}

/* ======================================================================== */
/*  ime_set_render — 描画バックエンドの差し替え (GUI v1.1 W2、K 依頼 2)       */
/*                                                                          */
/*  WM (gshell、CPL=0) が GFX 版の IME_Render 関数表を渡す。NULL で TVRAM 版  */
/*  に戻す。表の実体は呼び手が常駐している間だけ有効なので、gshell は終了時    */
/*  (CUI へ戻る前) に NULL を渡すこと。                                      */
/*  KAPI からは kernel/gui.c の gui_ime_set_render を経由する (owner 1 の     */
/*  CPL=0 の直呼びだけを通す門、2026-09-26)。ここはカーネル内の直呼び用で、   */
/*  呼び手を検査しない — gui_owner_exit(1) の NULL 戻しもここへ来る。         */
/* ======================================================================== */
void ime_set_render(void *table)
{
    const IME_Render *next = table ? (const IME_Render *)table : &g_ime_render_tvram;
    if (next != g_ime.render) {
        ime_cancel_input(); /* GUI/CUI input lifetime ends with its renderer. */
        g_ime_reader_owner = 0;
        g_ime.render = next;
    }
    g_ime_cui_redraw = table == NULL;
    /* Restore now and again at the first read after the shell reload clear. */
    if (ime_is_active()) preedit_draw();
}

static int ime_getkey_wait(int interruptible)
{
    if (!services_public) return kbd_getkey_wait(interruptible);
    int keydata;
    int result;

    if (con_sink_is_enabled()) return kbd_getkey_wait(interruptible);
    ime_prepare_cui_input();

    for (;;) {
        keydata = ime_delivery_take();
        if (keydata >= 0) {
            return keydata;
        }

        keydata = kbd_getkey_wait(interruptible);
        if (keydata < 0) return -1; /* key wait interrupted */

        /* Shift+Space: ON/OFF問わず常に検出 */
        if (((keydata >> 8) & SCANCODE_KEY) == KEY_SPACE &&
            (kbd_shift_state & SHIFT_SHIFT)) {
            ime_toggle();
            if (g_ime.commit_pos < g_ime.commit_len) {
                return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
            }
            continue;
        }

        /* FEP OFF: キーをそのまま返す */
        if (g_ime.mode == IME_MODE_OFF) {
            return keydata;
        }

        /* FEP ON: IME処理 */
        result = ime_delivery_process(keydata);
        if (result >= 0) {
            return result;
        }
        /* No output: 未確定、次のキーを待つ */
    }
}

int ime_trygetkey(void)
{
    if (!services_public) return kbd_trygetkey();
    int keydata;
    int result;

    if (con_sink_is_enabled()) return kbd_trygetkey();
    ime_prepare_cui_input();

    keydata = ime_delivery_take();
    if (keydata >= 0) {
        ime_note_cui_reader();
        return keydata;
    }

    keydata = kbd_trygetkey();
    if (keydata < 0) return -1;
    ime_note_cui_reader();

    /* Shift+Space: ON/OFF問わず常に検出 */
    if (((keydata >> 8) & SCANCODE_KEY) == KEY_SPACE &&
        (kbd_shift_state & SHIFT_SHIFT)) {
        ime_toggle();
        if (g_ime.commit_pos < g_ime.commit_len) {
            return (int)(u8)g_ime.commit_buf[g_ime.commit_pos++];
        }
        return -1;
    }

    /* FEP OFF: キーをそのまま返す */
    if (g_ime.mode == IME_MODE_OFF) {
        return keydata;
    }

    /* FEP ON: IME処理 */
    result = ime_delivery_process(keydata);
    if (result >= 0) {
        return result;
    }
    /* No output: 未確定、キーを消費したのでキーなし (-1) を返す */
    return -1;
}

#include "ime_dict_api.inc"

/* Cancel an input target's composition, without committing candidates.
 * Dictionary/mode stay available for the next reader. */
static void ime_cancel_input(void)
{
    candlist_clear();
    g_ime.state = IME_ST_INPUT;
    g_ime.kana_len = 0;
    g_ime.kana_buf[0] = '\0';
    g_ime.converting = 0;
    g_ime.result_count = g_ime.candidate_idx = g_ime.page = 0;
    g_ime.convert_len = 0;
    g_ime.commit_len = g_ime.commit_pos = 0;
    ime_delivery_clear();
    ime_rk_init(&g_ime.rk);
    tvram_set_scroll_reserve(g_ime.mode != IME_MODE_OFF ? 1 : 0);
    /* OFF leaves the last console row to the app; ON keeps the mode visible. */
    if (g_ime.mode != IME_MODE_OFF) preedit_draw();
}
int ime_getchar(void) { return ime_getchar_wait(0); }
int ime_getkey(void) { return ime_getkey_wait(0); }
int ime_getchar_kapi(void)
{
    int c;
    if (!con_sink_is_enabled() && ring3_call_from_user())
        g_ime_reader_owner = res_owner_get();
    c = ime_getchar_wait(1);
    if (c < 0) {
        if (!con_sink_is_enabled()) ime_cancel_input();
        ring3_abort_check();
    }
    return c;
}
int ime_getkey_kapi(void)
{
    int c;
    if (!con_sink_is_enabled() && ring3_call_from_user())
        g_ime_reader_owner = res_owner_get();
    c = ime_getkey_wait(1);
    if (c < 0) {
        if (!con_sink_is_enabled()) ime_cancel_input();
        ring3_abort_check();
    }
    return c;
}

/* Also covers abort/fault at a syscall boundary after input became ready.
 * A child that never read IME input must not cancel its parent's preedit. */
void ime_owner_exit(int owner)
{
    if (owner <= 0 || owner != g_ime_reader_owner) return;
    g_ime_reader_owner = 0;
    if (!con_sink_is_enabled()) ime_cancel_input();
}
