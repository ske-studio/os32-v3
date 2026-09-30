/* ======================================================================== */
/*  LIBOS32INPUT.H — OS32 入力抽象化ライブラリ 公開ヘッダ                    */
/*                                                                          */
/*  物理デバイス (キーボード・マウス・将来ゲームパッド) の入力を               */
/*  論理アクションに変換する抽象化ライブラリ。                                */
/*                                                                          */
/*  依存: libos32math (fix16_t), KernelAPI (kbd/mouse)                      */
/*  描画 (gfx/pyxel) には依存しない。                                        */
/* ======================================================================== */

#ifndef LIBOS32INPUT_H
#define LIBOS32INPUT_H

#include "os32_kapi_shared.h"    /* u8, u16, u32, i8, i16, i32, KernelAPI */
#include "libos32math.h"         /* fix16_t, FIX16_ONE, fix16_mul, fix16_clamp */

/* ====================================================================== */
/*  1. デバイス種別                                                        */
/* ====================================================================== */

#define INP_DEV_KEYBOARD  0
#define INP_DEV_MOUSE     1
#define INP_DEV_GAMEPAD   2   /* 将来用: シリアルブリッジ経由 */

/* ====================================================================== */
/*  2. 配列上限                                                            */
/* ====================================================================== */

#define INPUT_MAX_ACTIONS   32   /* 同時管理アクション上限 */
#define INPUT_MAX_BINDINGS  64   /* 同時バインディング上限 */
#define INPUT_CTX_MAX        4   /* コンテキスト保存スロット数 (P2) */

/* ====================================================================== */
/*  3. 修飾キー定数 (kbd.h の SHIFT_* を外部プログラムにも公開)             */
/* ====================================================================== */

#define INP_MOD_SHIFT   0x01
#define INP_MOD_CAPS    0x02
#define INP_MOD_KANA    0x04
#define INP_MOD_GRPH    0x08
#define INP_MOD_CTRL    0x10

/* ====================================================================== */
/*  4. アクション状態 (InputActionState)                                   */
/* ====================================================================== */

typedef struct {
    fix16_t value;              /* 現在の入力値 (0=未入力, FIX16_ONE=全押し) */
    fix16_t prev_value;         /* 前フレームの値 (トリガー/リリース判定用) */
    u16     hold_frames;        /* 押し続けフレーム数 (リピート判定用) */
    u16     _pad;
} InputActionState;

/* ====================================================================== */
/*  5. バインディング (InputBinding)                                       */
/*                                                                          */
/*  Phase 2 拡張: modifier_mask フィールド追加                              */
/*  modifier_mask == 0 の場合は修飾キーを問わない (P1互換)                   */
/* ====================================================================== */

typedef struct {
    u8      action_id;          /* 紐付け先アクションID (0-31) */
    u8      device;             /* INP_DEV_* */
    u8      modifier_mask;      /* 要求する修飾キー (INP_MOD_* の OR, 0=不問) */
    u8      _pad;
    u16     code;               /* キーボード: スキャンコード (0x00-0x7F)
                                   マウス: MOUSE_BTN_LEFT 等 */
    u16     _pad2;
    fix16_t scale;              /* 入力値のスケール
                                   デジタル: FIX16_ONE or -FIX16_ONE
                                   アナログ: 任意の倍率 */
} InputBinding;                 /* 合計 12バイト */

/* ====================================================================== */
/*  6. マウスキャッシュ                                                    */
/* ====================================================================== */

typedef struct {
    i16 x, y;                   /* 現在座標 */
    i16 dx, dy;                 /* 前フレームからの差分 */
    u8  buttons;                /* ボタンビットマスク */
    u8  polled;                 /* このフレームでpoll済みか */
} InputMouseCache;

/* ====================================================================== */
/*  7. コンテキストスロット (P2)                                           */
/* ====================================================================== */

typedef struct {
    InputBinding bindings[INPUT_MAX_BINDINGS];
    int          num_bindings;
    u8           used;          /* 1=このスロットにデータあり */
    u8           _pad[3];
} InputContext;

/* ====================================================================== */
/*  8. API — システム管理 (input_core.c)                                   */
/* ====================================================================== */

/* 初期化: KAPIポインタを保持、内部配列ゼロクリア */
int  input_init(KernelAPI *api);

/* 終了: 内部状態リセット */
void input_shutdown(void);

/* 毎フレーム1回呼ぶ (ゲームループの先頭)
 *
 * 処理内容:
 *   1. prev_value <- value をシフト
 *   2. 全アクションの value をリセット
 *   3. バインディング配列を走査し、KAPI経由で物理入力を取得
 *   4. scale を適用して value に合算
 *   5. value を -FIX16_ONE ~ FIX16_ONE にクランプ
 *   6. hold_frames を更新
 */
void input_update(void);

/* ====================================================================== */
/*  9. API — バインディング管理 (input_bind.c)                             */
/* ====================================================================== */

/* 物理入力→アクションの紐付けを追加 (修飾キー不問版)
 * 戻り値: 0=成功, -1=バインディング上限超過
 *
 * 使用例:
 *   input_bind(ACT_JUMP, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);   Space→Jump
 *   input_bind(ACT_JUMP, INP_DEV_MOUSE, MOUSE_BTN_LEFT, FIX16_ONE);
 *   input_bind(ACT_MOVE_X, INP_DEV_KEYBOARD, 0x3C, FIX16_ONE);  右→+1.0
 *   input_bind(ACT_MOVE_X, INP_DEV_KEYBOARD, 0x3B, -FIX16_ONE); 左→-1.0
 */
int  input_bind(int action_id, int device, int code, fix16_t scale);

/* 物理入力→アクションの紐付けを追加 (修飾キー指定版)
 * modifier_mask: INP_MOD_* の OR (Ctrl+S → INP_MOD_CTRL)
 * modifier_mask == 0 の場合は input_bind() と同等
 *
 * 使用例:
 *   input_bind_ex(ACT_SAVE, INP_DEV_KEYBOARD, 0x1F, FIX16_ONE, INP_MOD_CTRL);
 */
int  input_bind_ex(int action_id, int device, int code,
                   fix16_t scale, u8 modifier_mask);

/* 指定アクションの全バインディングを解除 */
void input_unbind(int action_id);

/* 全バインディングを解除 */
void input_unbind_all(void);

/* ====================================================================== */
/*  10. API — コンテキスト切替 (input_ctx.c) [P2]                          */
/* ====================================================================== */

/* 現在のバインディングをスロットに保存
 * slot: 0 ~ INPUT_CTX_MAX-1
 * 戻り値: 0=成功, -1=スロット番号不正
 */
int  input_save_context(int slot);

/* スロットからバインディングを復元
 * 戻り値: 0=成功, -1=スロット番号不正 or 未保存
 */
int  input_load_context(int slot);

/* ====================================================================== */
/*  11. API — キーコンフィグ保存・読み込み (input_config.c) [P2]            */
/* ====================================================================== */

/* 現在のバインディングをファイルに保存
 * 戻り値: 0=成功, -1=エラー
 */
int  input_save_config(const char *path);

/* ファイルからバインディングを読み込み (既存バインディングは全解除)
 * 戻り値: 読み込んだバインディング数, -1=エラー
 */
int  input_load_config(const char *path);

/* ====================================================================== */
/*  12. API — アクション状態取得 (input_query.c)                           */
/* ====================================================================== */

/* 現在押下中か (value != 0 なら非0を返す) */
int input_pressed(int action_id);

/* 押した瞬間か (前フレーム未入力→今フレーム入力) */
int input_triggered(int action_id);

/* 離した瞬間か (前フレーム入力→今フレーム未入力) */
int input_released(int action_id);

/* アナログ値取得 (-FIX16_ONE ~ FIX16_ONE)
 * デジタル入力の場合: 0 or FIX16_ONE (or -FIX16_ONE)
 * 左右同時押しの場合: 0 (合算でキャンセル)
 */
fix16_t input_value(int action_id);

/* リピート判定 (pyxel_btnp 互換)
 * hold: 最初のリピートまでのフレーム数 (0=リピートなし)
 * repeat: 以降のリピート間隔フレーム数
 */
int input_held(int action_id, int hold, int repeat);

/* ====================================================================== */
/*  13. API — ユーティリティ                                               */
/* ====================================================================== */

/* 修飾キー状態取得 (kbd_get_modifiers ラッパー)
 * 戻り値: INP_MOD_SHIFT | INP_MOD_CTRL 等のビットマスク
 */
u32 input_modifiers(void);

/* マウス座標取得 (mouse_poll キャッシュから) */
void input_get_mouse(i16 *x, i16 *y);

/* マウス差分取得 */
void input_get_mouse_delta(i16 *dx, i16 *dy);

/* マウスボタン押下判定 (MOUSE_BTN_LEFT 等) */
int input_mouse_btn(int btn);

#endif /* LIBOS32INPUT_H */
