/* ======================================================================== */
/*  MD_RENDER.H - Markdown描画エンジン                                       */
/*                                                                          */
/*  GFXバックバッファ上にMarkdownドキュメントを描画するレンダラー。           */
/*  libos32gfx + kcg (KCGスケーリング) に依存。                             */
/*                                                                          */
/*  使い方:                                                                 */
/*    md_render_init(api);                                                  */
/*    total_h = md_layout(doc);                                             */
/*    md_render_page(doc, scroll_y);                                        */
/*    md_render_statusbar("file.md", scroll_y, total_h);                    */
/* ======================================================================== */

#ifndef MD_RENDER_H
#define MD_RENDER_H

#include "libos32md.h"
#include "os32api.h"

/* ======================================================================== */
/*  画面定数                                                                */
/* ======================================================================== */

#define MDR_SCREEN_W       640
#define MDR_SCREEN_H       400

#define MDR_MARGIN_LEFT    16
#define MDR_MARGIN_RIGHT   16
#define MDR_CONTENT_W      (MDR_SCREEN_W - MDR_MARGIN_LEFT - MDR_MARGIN_RIGHT)

#define MDR_STATUS_H       20
#define MDR_PAGE_H         (MDR_SCREEN_H - MDR_STATUS_H)

/* ======================================================================== */
/*  パレットインデックス                                                    */
/* ======================================================================== */

enum {
    MDR_COL_BG         = 0,
    MDR_COL_H1         = 1,
    MDR_COL_H2         = 2,
    MDR_COL_H3         = 3,
    MDR_COL_TEXT       = 4,
    MDR_COL_CODE_FG    = 5,
    MDR_COL_CODE_BG    = 6,
    MDR_COL_RULER      = 7,
    MDR_COL_BOLD       = 8,
    MDR_COL_LINK       = 9,
    MDR_COL_BULLET     = 10,
    MDR_COL_H1_BAR     = 11,
    MDR_COL_STATUS_BG  = 12,
    MDR_COL_STATUS_FG  = 13,
    MDR_COL_SEARCH_BG  = 14,
    MDR_COL_QUOTE_BAR  = 15
};

/* ======================================================================== */
/*  検索状態 (mdview.cから設定される)                                        */
/* ======================================================================== */

#define MDR_SEARCH_MAX 32

typedef struct {
    char term[MDR_SEARCH_MAX];
    int  len;
    int  active;
    int  current_idx;     /* 現在のマッチノードindex */
} MdSearchState;

/* ======================================================================== */
/*  API                                                                      */
/* ======================================================================== */

/*
 * md_render_init - レンダラー初期化
 *
 * KernelAPIポインタを設定する。libos32gfx初期化後に呼ぶこと。
 */
void md_render_init(KernelAPI *api);

/*
 * md_layout - ドキュメントのレイアウト計算
 *
 * 各ノードのY座標と高さを計算し、仮想ドキュメント全体の高さを返す。
 * ドキュメント変更時 (別ファイル読み込み後) に再度呼ぶこと。
 *
 * 戻り値: 仮想ドキュメントの総ピクセル高さ
 */
int md_layout(MdDocument *doc);

/*
 * md_render_reset_scroll - スクロール状態をリセット (フル再描画を強制)
 */
void md_render_reset_scroll(void);

/*
 * md_render_page - ページ描画 (差分最適化あり)
 *
 * scroll_y に応じた部分のみを再描画する。
 * 大きなジャンプ時はフル再描画される。
 */
void md_render_page(MdDocument *doc, int scroll_y);

/*
 * md_render_statusbar - ステータスバー描画
 */
void md_render_statusbar(const char *filename, int scroll_y, int total_h);

/*
 * md_render_get_node_y - 指定ノードのY座標を返す
 *
 * 検索結果や目次ジャンプで、特定ノードのY座標が必要な場合に使う。
 */
int md_render_get_node_y(int node_idx);

/*
 * md_render_set_search - 検索状態を設定する
 */
void md_render_set_search(const MdSearchState *state);

/*
 * md_render_get_search - 現在の検索状態を取得する
 */
const MdSearchState *md_render_get_search(void);

/* mdviewパレット定義 (GFX初期化で使用) */
extern const unsigned char md_palette[16][3];

#endif /* MD_RENDER_H */
