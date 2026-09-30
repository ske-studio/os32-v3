/* ======================================================================== */
/*  LIBOS32ECS.H — OS32 軽量ECSライブラリ 公開ヘッダ                        */
/*                                                                          */
/*  Entity-Component-System アーキテクチャによるゲームオブジェクト管理。      */
/*  SoA配列ベース、ビットマスクコンポーネント管理、遅延破棄。                */
/*                                                                          */
/*  依存: libos32math (fix16_t), KernelAPI (mem_alloc不使用)                */
/*  描画 (gfx/tilemap) には依存しない。                                      */
/* ======================================================================== */

#ifndef LIBOS32ECS_H
#define LIBOS32ECS_H

#include "os32_kapi_shared.h"    /* u8, u16, u32, i8, i16, i32 */
#include "libos32math.h"         /* fix16_t */

/* ====================================================================== */
/*  1. 定数                                                                */
/* ====================================================================== */

#define ECS_MAX_ENTITIES    128  /* 同時管理Entity上限 */
#define ECS_MAX_SYSTEMS      16  /* System登録上限 */
#define ECS_MAX_CUSTOM_COMP  22  /* カスタムコンポーネント登録上限 (bit10~31) */
#define ECS_INVALID         (-1) /* 無効なEntity ID */

/* Entity 生存状態 */
#define ECS_DEAD              0  /* 未使用スロット */
#define ECS_ALIVE             1  /* 生存中 */
#define ECS_PENDING_DESTROY   2  /* 破棄予約済み (フレーム末尾で解放) */

/* ====================================================================== */
/*  2. コンポーネント種別ビット (u32 comp_mask)                            */
/* ====================================================================== */

/* === システム予約 (bit 0~9) === */
#define COMP_TRANSFORM    0x0001u
#define COMP_VELOCITY     0x0002u
#define COMP_SPRITE       0x0004u
#define COMP_COLLIDER     0x0008u
#define COMP_HEALTH       0x0010u
#define COMP_CHEM         0x0020u
#define COMP_AI           0x0040u
#define COMP_TIMER        0x0080u
#define COMP_TAG          0x0100u
#define COMP_ANIM         0x0200u

/* === ゲーム固有拡張用 (bit 10~31) === */
/* 使用前に ecs_register_custom_comp() で登録が必要 */
#define COMP_SYSTEM_MASK  0x000003FFu  /* bit 0~9: システム予約 */
#define COMP_CUSTOM_MASK  0xFFFFFC00u  /* bit 10~31: ゲーム固有 */

/* ====================================================================== */
/*  3. Entity型                                                            */
/* ====================================================================== */

typedef i16  ecs_entity_t;

/* Entity メタデータ (内部管理) */
typedef struct {
    u32  comp_mask;     /* 保有コンポーネントのビットマスク */
    u8   alive;         /* ECS_DEAD / ECS_ALIVE / ECS_PENDING_DESTROY */
    u8   layer;         /* 更新レイヤー (0=最優先, 3=最後) */
    u16  gen;           /* 世代番号 (ダングリング防止) */
} EcsEntity;            /* 8 バイト */

/* ====================================================================== */
/*  4. コンポーネント構造体 (SoA 配列)                                     */
/* ====================================================================== */

/* Transform: 座標 (fix16_t でサブピクセル精度) */
typedef struct {
    fix16_t x, y;           /* ワールド座標 (Q16.16) */
    i16     tile_x, tile_y; /* タイル座標 (整数、mapとの連携用) */
} CompTransform;            /* 12B */

/* Velocity: 移動 */
typedef struct {
    fix16_t vx, vy;         /* 速度 (pixel/frame, Q16.16) */
    fix16_t ax, ay;         /* 加速度 */
} CompVelocity;             /* 16B */

/* Sprite: 描画情報 */
typedef struct {
    u16  tile_id;           /* libtilemap タイルID */
    u16  frame;             /* アニメーションフレーム */
    u8   palette;           /* パレットスロット */
    u8   flip;              /* bit0=H反転, bit1=V反転 */
    i8   priority;          /* 描画優先度 (Zオーダー) */
    u8   visible;           /* 表示ON/OFF */
} CompSprite;               /* 8B */

/* Collider: AABB衝突判定 */
typedef struct {
    i8   ox, oy;            /* Entity座標からのオフセット */
    u8   w, h;              /* 判定矩形サイズ */
    u8   layer_mask;        /* 衝突レイヤー (ビットマスク) */
    u8   _pad;
} CompCollider;             /* 6B */

/* Health: HP管理 */
typedef struct {
    i16  hp;                /* 現在HP */
    i16  max_hp;            /* 最大HP */
    u8   invincible;        /* 無敵残りフレーム */
    u8   _pad;
} CompHealth;               /* 6B */

/* Chem: 化学エンジンブリッジ */
typedef struct {
    i16  chem_id;           /* libos32chem の ChemObject ID (-1=未接続) */
} CompChem;                 /* 2B */

/* AI: 状態機械 */
typedef struct {
    u8   state;             /* 現在のAI状態ID */
    u8   prev_state;        /* 前フレームの状態 */
    u16  state_timer;       /* 現状態の経過フレーム */
    i16  target_entity;     /* 追跡対象Entity ID (-1=なし) */
    u16  _pad;
} CompAI;                   /* 8B */

/* Timer: 汎用タイマー */
typedef struct {
    u16  remaining;         /* 残りフレーム (0=満了) */
    u16  duration;          /* 初期値 (リセット用) */
    u8   auto_repeat;       /* 1=満了時自動リセット */
    u8   callback_id;       /* コールバック識別子 */
} CompTimer;                /* 6B */

/* Tag: グループ分類 */
typedef struct {
    u16  tag;               /* ゲーム固有タグ (PLAYER/ENEMY/ITEM等) */
    u16  sub_tag;           /* サブ分類 */
} CompTag;                  /* 4B */

/* Anim: アニメーション制御 */
typedef struct {
    u8   anim_id;           /* アニメーション定義ID */
    u8   frame_idx;         /* 現在のフレームインデックス */
    u8   speed;             /* フレーム間隔 (ゲームフレーム数) */
    u8   counter;           /* フレーム間カウンタ */
    u8   loop;              /* 1=ループ, 0=1回再生 */
    u8   done;              /* 1=再生完了 */
} CompAnim;                 /* 6B */

/* ====================================================================== */
/*  5. System 関数型                                                       */
/* ====================================================================== */

typedef void (*ecs_system_fn)(void);

/* ====================================================================== */
/*  6. コールバック型                                                      */
/* ====================================================================== */

typedef void (*ecs_lifecycle_cb)(ecs_entity_t e);
typedef void (*ecs_timer_cb)(ecs_entity_t e, u8 callback_id);

/* Chem アダプタ: libos32chem に依存しないための間接呼出し
 * ゲーム側がこの関数内で chem_get() による座標同期を行う
 * e: ECS Entity ID, chem_id: ChemObject ID, t: Transform (読み書き可) */
typedef void (*ecs_chem_adapter_fn)(ecs_entity_t e, i16 chem_id,
                                   CompTransform *t);

/* ====================================================================== */
/*  7. API — システム管理 (ecs_core.c)                                     */
/* ====================================================================== */

/* 初期化: 全配列ゼロクリア、フリーリスト構築 */
int  ecs_init(void);

/* 終了: 全Entity解放、内部状態リセット */
void ecs_shutdown(void);

/* ワールドリセット: 全Entity破棄 + フリーリスト再構築 */
void ecs_reset(void);

/* Entity生成: フリーリストから取得
 * 戻り値: Entity ID (0~127), 満杯時 ECS_INVALID */
ecs_entity_t ecs_create(void);

/* Entity破棄予約 (遅延破棄)
 * alive を ECS_PENDING_DESTROY にマークし、ecs_cleanup() で実際に解放 */
void ecs_destroy(ecs_entity_t e);

/* Entity生存チェック (alive == ECS_ALIVE) */
int  ecs_alive(ecs_entity_t e);

/* アクティブEntity数 */
int  ecs_active_count(void);

/* フレーム更新: 全System を優先度順に実行 */
void ecs_update(void);

/* 遅延破棄の実行: PENDING_DESTROY → DEAD + フリーリストに戻す */
void ecs_cleanup(void);

/* ====================================================================== */
/*  8. API — コンポーネント操作 (ecs_component.c)                          */
/* ====================================================================== */

/* コンポーネント追加 (comp_bit は単一ビットまたは複数ビットのOR)
 * カスタムビットが未登録の場合は -1 を返す
 * 戻り値: 0=成功, -1=未登録カスタムビット */
int  ecs_add(ecs_entity_t e, u32 comp_bit);

/* コンポーネント除去 */
void ecs_remove(ecs_entity_t e, u32 comp_bit);

/* mask に含まれる全コンポーネントを保有しているか */
int  ecs_has(ecs_entity_t e, u32 mask);

/* 型付きアクセサ (SoA配列への直接ポインタ) */
CompTransform *ecs_get_transform(ecs_entity_t e);
CompVelocity  *ecs_get_velocity(ecs_entity_t e);
CompSprite    *ecs_get_sprite(ecs_entity_t e);
CompCollider  *ecs_get_collider(ecs_entity_t e);
CompHealth    *ecs_get_health(ecs_entity_t e);
CompChem      *ecs_get_chem(ecs_entity_t e);
CompAI        *ecs_get_ai(ecs_entity_t e);
CompTimer     *ecs_get_timer(ecs_entity_t e);
CompTag       *ecs_get_tag(ecs_entity_t e);
CompAnim      *ecs_get_anim(ecs_entity_t e);

/* ====================================================================== */
/*  9. API — カスタムコンポーネント登録 (ecs_component.c)                  */
/* ====================================================================== */

/* ゲーム固有コンポーネントビットの登録
 * comp_bit: COMP_CUSTOM_MASK 範囲のビット (例: 0x0400)
 * name: デバッグ表示用の名前 (例: "INVENTORY")
 * 戻り値: 0=成功, -1=システム予約侵入, -2=二重登録, -3=上限超過 */
int  ecs_register_custom_comp(u32 comp_bit, const char *name);

/* 登録済みカスタムコンポーネント数 */
int  ecs_custom_comp_count(void);

/* ====================================================================== */
/*  10. API — System 登録・制御 (ecs_system.c)                             */
/* ====================================================================== */

/* System 登録
 * fn: 毎フレーム呼ばれる処理関数
 * priority: 実行優先度 (0=最優先, 数値が小さいほど先に実行)
 * required_mask: このSystemが処理対象とするコンポーネントの組み合わせ
 * 戻り値: System ID (0~15), 満杯時 -1 */
int  ecs_register_system(ecs_system_fn fn, int priority,
                         u32 required_mask);

/* System の有効/無効切替 */
void ecs_enable_system(int sys_id, int enable);

/* ====================================================================== */
/*  11. API — クエリ (ecs_query.c)                                         */
/* ====================================================================== */

/* comp_mask に一致する全Entity を列挙
 * 戻り値: 見つかった数 (max_count で打ち切り) */
int  ecs_query(u32 mask, ecs_entity_t *out, int max_count);

/* タグで最初に見つかった Entity を返す (見つからなければ ECS_INVALID) */
ecs_entity_t ecs_find_by_tag(u16 tag);

/* 矩形範囲内の Entity を列挙 (COMP_TRANSFORM 必須)
 * 戻り値: 見つかった数 */
int  ecs_find_in_rect(i16 x, i16 y, i16 w, i16 h,
                      ecs_entity_t *out, int max_count);

/* ====================================================================== */
/*  12. API — コールバック (ecs_core.c)                                    */
/* ====================================================================== */

/* Entity 生成時コールバック設定 */
void ecs_on_create(ecs_lifecycle_cb cb);

/* Entity 破棄時コールバック設定 (cleanup 実行時に呼ばれる) */
void ecs_on_destroy(ecs_lifecycle_cb cb);

/* タイマー満了コールバック設定 */
void ecs_set_timer_callback(ecs_timer_cb cb);

/* ====================================================================== */
/*  13. 組み込み System 関数 (ecs_system.c)                                */
/* ====================================================================== */

/* Velocity → Transform 適用 (加速度→速度→座標) */
void sys_physics(void);

/* アニメーションフレーム進行 */
void sys_anim(void);

/* タイマー減算・満了コールバック */
void sys_timer(void);

/* HP=0 の Entity に破棄予約 */
void sys_health(void);

/* ChemObject ↔ Entity 座標同期 (アダプタ方式) */
void sys_chem_sync(void);

/* Chem アダプタの設定 (sys_chem_sync 内で呼ばれる) */
void ecs_set_chem_adapter(ecs_chem_adapter_fn fn);

/* ====================================================================== */
/*  14. デバッグ                                                           */
/* ====================================================================== */

/* 全Entity の状態ダンプ (kprintf経由) */
void ecs_debug_dump(void);

#endif /* LIBOS32ECS_H */
