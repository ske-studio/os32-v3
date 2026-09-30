/* ======================================================================== */
/*  LIBFILER.H - GFXベースファイラーライブラリ                               */
/*                                                                          */
/*  GFXモード上でファイル選択UIを表示する独立ライブラリ。                    */
/*  VZのfiler.cからロジックを分離し、GFX描画で再実装。                      */
/*                                                                          */
/*  使い方:                                                                 */
/*    filer_init(api);                                                      */
/*    result = filer_open("/", filter);  <- モーダルUI開始                   */
/*    if (result) path = filer_get_selected_path();                         */
/* ======================================================================== */

#ifndef LIBFILER_H
#define LIBFILER_H

#include "os32api.h"

/* 定数 */
#define FILER_MAX_ENTRIES  128
#define FILER_MAX_PATH     256
#define FILER_NAME_LEN     64

/* ファイルエントリ */
typedef struct {
    char name[FILER_NAME_LEN];
    u32  size;
    u8   is_dir;
} FilerEntry;

/* ファイラー状態 */
typedef struct {
    int active;
    FilerEntry entries[FILER_MAX_ENTRIES];
    int entry_count;
    /* 表に載せられなかった件数 (名前が FILER_NAME_LEN に収まらない /
     * FILER_MAX_ENTRIES を超えた)。切った名前を載せると別のファイルの
     * パスを返してしまうので、載せずに数だけ報せる (票 T23)。 */
    int dropped_count;
    int cursor_idx;
    int scroll_top;
    char current_dir[FILER_MAX_PATH];
    char selected_path[FILER_MAX_PATH];
    const char *filter_ext;   /* 拡張子フィルタ (NULLなら全表示) */
} FilerState;

/* ======================================================================== */
/*  API                                                                      */
/* ======================================================================== */

/* 初期化 */
void filer_init(KernelAPI *api);

/*
 * filer_open - モーダルファイル選択UIを表示
 *
 * dir:    初期ディレクトリ
 * filter: 拡張子フィルタ (例: ".md" or ".1", NULLなら全表示)
 *
 * 戻り値: 1=ファイル選択済み, 0=キャンセル
 *
 * 選択後は filer_get_selected_path() でフルパスを取得
 */
int filer_open(const char *dir, const char *filter);

/* 選択されたファイルのフルパスを取得 */
const char *filer_get_selected_path(void);

#endif /* LIBFILER_H */
