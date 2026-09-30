# libos32ecs — ゲームオブジェクト管理ライブラリ設計書

*策定: 2026-04-28*

> OS32のゲーム開発基盤として、画面上のキャラクター・アイテム・エフェクト等の
> データ構造化・更新順序・ライフサイクルを管理する軽量ECSライブラリの設計書。

---

## 1. 設計背景

### 1.1 なぜ ECS が必要か

現在のOS32ゲームライブラリ群には**オブジェクトの統合管理層が欠けている**:

| 問題 | 現状 |
|------|------|
| オブジェクト散在 | `ChemObject` (libos32chem) は化学専用。汎用キャラ管理なし |
| 更新順序の未定義 | input→物理→化学→描画の呼出順序がゲーム側に丸投げ |
| ライフサイクル未管理 | 生成・初期化・更新・破棄の統一フローがない |
| コンポーネント非分離 | 座標・HP・描画情報がモノリシック構造体に混在 |

### 1.2 既存ライブラリとの関係

```
libos32math   (依存なし — fix16_t, Vec2)
     ^
libos32input  (math + KAPI)
     ^
libos32ecs ◄━━ 本ライブラリ (math + input + KAPI)
     ^
     ├── libos32chem   (ecs の ChemComponent 経由で連携)
     ├── libos32map    (マップ上の Entity 配置・衝突判定)
     ├── libtilemap    (Entity の描画座標→タイル合成)
     ├── libos32asset  (スプライトデータのロード)
     └── libos32snd    (SE トリガー)
```

**重要**: libos32ecs は**描画ライブラリ (libos32gfx, libtilemap) に依存しない**。
描画はゲーム側の System 関数が行う。ECS は座標・状態の管理のみ。

### 1.3 ChemObject との関係 — 統合か共存か

**結論: 共存 (ブリッジ方式)**

| 方式 | メリット | デメリット |
|------|---------|-----------|
| 統合 (ChemObjectをECSに吸収) | 一元管理 | libos32chem の破壊的変更、SQLiteルール参照が複雑化 |
| **共存 (ブリッジ)** | **既存libos32chemは無変更** | **ID対応テーブルが必要** |

ECS Entity に `ChemComponent` を付与し、`chem_id` フィールドで
libos32chem の `ChemObject` と1:1対応させる。化学シミュレーションは
libos32chem が独立実行し、ECS側は結果を参照するだけ。

### 1.4 設計思想

- **Structure of Arrays (SoA)**: コンポーネント種別ごとに配列を分離し、キャッシュ効率を最大化 (i386 L1=8KB を活用)
- **ビットマスクベースのコンポーネント管理**: 各Entity が持つコンポーネントを `u32` ビットマスクで管理。最大32種
- **固定配列 + フリーリスト**: `mem_alloc` 不使用。全配列は BSS に静的確保
- **C89 整数演算のみ**: FPU 不使用 (libos32math の fix16_t を活用)

---

## 2. コアアーキテクチャ

### 2.1 Entity

```c
#define ECS_MAX_ENTITIES  128
#define ECS_INVALID       (-1)

typedef i16  ecs_entity_t;

typedef struct {
    u32  comp_mask;     /* 保有コンポーネントのビットマスク */
    u8   alive;         /* 0=未使用, 1=生存, 2=破棄予約 */
    u8   layer;         /* 更新レイヤー (0=最優先, 3=最後) */
    u16  gen;           /* 世代番号 (再利用時のダングリング防止) */
} EcsEntity;            /* 8 バイト */
```

### 2.2 コンポーネント種別ビット

```c
/* === システム予約 (bit 0~9) === */
#define COMP_TRANSFORM    0x0001
#define COMP_VELOCITY     0x0002
#define COMP_SPRITE       0x0004
#define COMP_COLLIDER     0x0008
#define COMP_HEALTH       0x0010
#define COMP_CHEM         0x0020
#define COMP_AI           0x0040
#define COMP_TIMER        0x0080
#define COMP_TAG          0x0100
#define COMP_ANIM         0x0200

/* === ゲーム固有拡張用 (bit 10~31) === */
/* 使用前に ecs_register_custom_comp() で登録が必要 */
#define COMP_SYSTEM_MASK  0x000003FF  /* bit 0~9: システム予約 */
#define COMP_CUSTOM_MASK  0xFFFFFC00  /* bit 10~31: ゲーム固有 */
```

### 2.3 ゲーム固有コンポーネントの保護機能

**問題**: ゲーム側が自由に bit 10~31 を使う場合、ビット衝突や
未登録コンポーネントへの誤アクセスが発生するリスクがある。

**対策**: 登録制 + バリデーション + デバッグアサートの3段階保護。

```c
/* ゲーム固有コンポーネント登録 (ecs_init() の後に呼ぶ)
 * comp_bit: COMP_CUSTOM_MASK 範囲のビット (例: 0x0400)
 * name: デバッグ表示用の名前 (例: "INVENTORY")
 * 戻り値: 0=成功, -1=システム予約ビットへの侵入,
 *         -2=同一ビット二重登録, -3=登録上限超過
 */
int ecs_register_custom_comp(u32 comp_bit, const char *name);

/* 登録済みカスタムコンポーネント数 */
int ecs_custom_comp_count(void);
```

**3段階の保護メカニズム**:

| レベル | 保護内容 | 実装 |
|--------|---------|------|
| L1: 登録チェック | `ecs_add()` でカスタムビットが未登録なら拒否 (-1 返却) | `registered_mask` を内部保持 |
| L2: 範囲ガード | システム予約ビット (0~9) へのカスタム登録を拒否 | `comp_bit & COMP_SYSTEM_MASK` チェック |
| L3: デバッグアサート | `ecs_get_*()` 呼出時に `ecs_has()` を内部チェック (デバッグビルドのみ) | `#ifdef ECS_DEBUG` でアサート |

```c
/* ゲーム側の使用例 */
#define COMP_INVENTORY  0x0400  /* bit 10 */
#define COMP_QUEST      0x0800  /* bit 11 */

typedef struct {
    u16 item_id[8];
    u8  count[8];
    u8  num_items;
    u8  _pad;
} CompInventory;

static CompInventory g_inventory[ECS_MAX_ENTITIES]; /* ゲーム側SoA */

void game_init(void)
{
    ecs_init();
    /* カスタムコンポーネント登録 (未登録のまま ecs_add すると -1) */
    ecs_register_custom_comp(COMP_INVENTORY, "INVENTORY");
    ecs_register_custom_comp(COMP_QUEST, "QUEST");
}

CompInventory *game_get_inventory(ecs_entity_t e)
{
    /* ecs_has チェックはゲーム側の責務 (デバッグビルドではECS内部もアサート) */
    return &g_inventory[e];
}
```

> **設計判断**: SoA 配列そのものはゲーム側が管理する。libos32ecs が管理するのは
> `comp_mask` のビットフラグのみ。これによりライブラリ側のメモリ使用量が固定のまま、
> ゲーム側は任意のデータ構造を自由に設計できる。

### 2.3 コンポーネント構造体 (SoA 配列)

```c
typedef struct {
    fix16_t x, y;           /* ワールド座標 (Q16.16) */
    i16     tile_x, tile_y; /* タイル座標 (整数) */
} CompTransform;            /* 12B */

typedef struct {
    fix16_t vx, vy;         /* 速度 */
    fix16_t ax, ay;         /* 加速度 */
} CompVelocity;             /* 16B */

typedef struct {
    u16  tile_id;
    u16  frame;
    u8   palette;
    u8   flip;              /* bit0=H, bit1=V */
    i8   priority;          /* Zオーダー */
    u8   visible;
} CompSprite;               /* 8B */

typedef struct {
    i8   ox, oy;            /* オフセット */
    u8   w, h;              /* AABB サイズ */
    u8   layer_mask;
    u8   _pad;
} CompCollider;             /* 6B */

typedef struct {
    i16  hp, max_hp;
    u8   invincible;
    u8   _pad;
} CompHealth;               /* 6B */

typedef struct {
    i16  chem_id;           /* ChemObject ID (-1=未接続) */
} CompChem;                 /* 2B */

typedef struct {
    u8   state, prev_state;
    u16  state_timer;
    i16  target_entity;
    u16  _pad;
} CompAI;                   /* 8B */

typedef struct {
    u16  remaining, duration;
    u8   auto_repeat;
    u8   callback_id;
} CompTimer;                /* 6B */

typedef struct {
    u16  tag, sub_tag;
} CompTag;                  /* 4B */

typedef struct {
    u8   anim_id, frame_idx;
    u8   speed, counter;
    u8   loop, done;
} CompAnim;                 /* 6B */
```

### 2.4 メモリ使用量

| 項目 | サイズ |
|------|--------|
| EcsEntity[128] | 1,024B |
| 全コンポーネント配列 (10種) | 9,472B |
| System配列・フリーリスト等 | ~512B |
| **合計** | **~11KB** |

全配列はBSS配置のため、ヒープ消費ゼロ。

---

## 3. ライフサイクル管理

### 3.1 Entity 状態遷移

```
ecs_create() → ALIVE (alive=1)
    ↓ ecs_destroy()
PENDING_DESTROY (alive=2)
    ↓ ecs_cleanup() (フレーム末尾)
DEAD (alive=0) → フリーリストに戻る → ecs_create() で再利用
```

**遅延破棄**: `ecs_destroy()` は即座に削除せず `alive=2` にマーク。
フレーム末尾の `ecs_cleanup()` で実際の解放を行い、System 走査中の配列破壊を防止。

### 3.2 フレーム更新順序

```
1. input_update()         ← libos32input (外部)
2. ecs_update()           ← 登録済み System を優先度順に実行
     sys_input → sys_ai → sys_physics → sys_collision →
     sys_chem_sync → sys_anim → sys_timer → sys_health
3. chem_update()          ← libos32chem (外部)
4. ecs_cleanup()          ← PENDING_DESTROY 解放
5. (ゲーム側描画)          ← tilemap + gfx_present
```

---

## 4. API 設計

### 4.1 System 登録

```c
#define ECS_MAX_SYSTEMS  16
typedef void (*ecs_system_fn)(void);

int  ecs_register_system(ecs_system_fn fn, int priority, u32 required_mask);
void ecs_enable_system(int sys_id, int enable);
```

### 4.2 組み込み System

| System | required_mask | 処理 |
|--------|--------------|------|
| `sys_physics` | TRANSFORM \| VELOCITY | vx→x, vy→y 加算 |
| `sys_anim` | SPRITE \| ANIM | フレーム進行 |
| `sys_timer` | TIMER | remaining 減算 |
| `sys_health` | HEALTH | hp≤0 で destroy 予約 |
| `sys_chem_sync` | TRANSFORM \| CHEM | 座標同期 |

### 4.3 Entity 管理 API

```c
int           ecs_init(void);
void          ecs_shutdown(void);
void          ecs_reset(void);
ecs_entity_t  ecs_create(void);
void          ecs_destroy(ecs_entity_t e);
int           ecs_alive(ecs_entity_t e);
int           ecs_active_count(void);
void          ecs_update(void);
void          ecs_cleanup(void);
```

### 4.4 コンポーネント操作 API

```c
void ecs_add(ecs_entity_t e, u32 comp_bit);
void ecs_remove(ecs_entity_t e, u32 comp_bit);
int  ecs_has(ecs_entity_t e, u32 mask);

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
```

### 4.5 クエリ API

```c
int ecs_query(u32 mask, ecs_entity_t *out, int max_count);
ecs_entity_t ecs_find_by_tag(u16 tag);
int ecs_find_in_rect(i16 x, i16 y, i16 w, i16 h,
                     ecs_entity_t *out, int max_count);
```

### 4.6 コールバック

```c
typedef void (*ecs_lifecycle_cb)(ecs_entity_t e);
void ecs_on_create(ecs_lifecycle_cb cb);
void ecs_on_destroy(ecs_lifecycle_cb cb);

typedef void (*ecs_timer_cb)(ecs_entity_t e, u8 callback_id);
void ecs_set_timer_callback(ecs_timer_cb cb);
```

---

## 5. 既存ライブラリとの具体的連携

### 5.1 libos32chem — ブリッジ方式

Entity に `COMP_CHEM` を追加し `chem_id` で ChemObject と1:1対応。
`sys_chem_sync` が毎フレーム座標を同期。化学エンジン側は無変更。

### 5.2 libos32map — 衝突・イベント

`sys_physics` 内で `map_is_passable()` をチェックし壁衝突を処理。
`map_check_step()` でワープ・エンカウント判定。

### 5.3 libos32input — プレイヤー操作

ゲーム側の `sys_player_input` System が `input_value()` → `CompVelocity` に変換。

### 5.4 libtilemap — 描画

ゲーム側の描画 System が `CompTransform + CompSprite` を読み、
`tilemap_set()` でスプライトレイヤーに配置。

### 5.5 libos32asset — リソース

スプライトシートは `asset_load()` → `tilemap_load_asset()` で管理。
Entity のコンポーネントデータは BSS 静的配列であり、アセット管理対象外。

---

## 6. ディレクトリ構成

```
programs/libos32ecs/
    libos32ecs.h         公開ヘッダ
    ecs_core.c           Entity 管理 (create/destroy/cleanup)
    ecs_component.c      コンポーネント追加・除去・アクセサ
    ecs_system.c         System 登録・実行・組み込みSystem
    ecs_query.c          クエリ (マスク/タグ/範囲検索)
```

---

## 7. 実装フェーズ

### Phase 1: コア基盤

- [ ] `libos32ecs.h` ヘッダ作成
- [ ] `ecs_core.c` — Entity CRUD, フリーリスト, 遅延破棄
- [ ] `ecs_component.c` — SoA 配列, add/remove/has/get
- [ ] Makefile 統合
- [ ] `ecs_test.c` テストプログラム

### Phase 2: System フレームワーク

- [ ] `ecs_system.c` — System 登録/実行
- [ ] 組み込み System: sys_physics, sys_timer, sys_health, sys_anim
- [ ] フレーム制御: ecs_update() / ecs_cleanup()

### Phase 3: クエリ + chem連携

- [ ] `ecs_query.c` — マスク/タグ/矩形検索
- [ ] `sys_chem_sync` 組み込み System
- [ ] ライフサイクルコールバック
- [ ] `ecs_demo.c` デモプログラム

### Phase 4: ゲーム統合

- [ ] libos32map 連携 (衝突応答)
- [ ] libos32input 連携 (プレイヤー操作)
- [ ] RPGデモ: プレイヤー + NPC + マップ遷移

---

## 8. 設計判断ポイント

### Q: ECS_MAX_ENTITIES=128 は十分か？

**十分 (承認済み)。** PC-98の描画性能では同時描画可能なスプライトは50-80個。
libos32chem の `CHEM_MAX_OBJECTS=128` と一致。

### Q: ゲーム固有コンポーネントの追加方法は？

bit 10~31 をゲーム側で `#define` し、ゲーム側に並列の SoA 配列を置く。
libos32ecs のコア配列には触れない。

**システム側の保護機能** (§2.3 参照):
- `ecs_register_custom_comp()` による**登録制** — 未登録ビットの `ecs_add()` は拒否
- **システム予約ビット (0~9) への侵入防止** — カスタム登録時にマスクチェック
- **デバッグアサート** (`#ifdef ECS_DEBUG`) — `ecs_has()` 未チェックのアクセスを検出
- カスタムコンポーネント上限は **22個** (bit 10~31)

### Q: libpyxel との関係は？

**libpyxel は廃止。** 新規ゲームは libos32ecs + libos32input + libtilemap を
直接使用する。libpyxel のコードは参考実装として残すが、積極的なメンテナンスは行わない。
`pyxel_run()` のupdate/drawコールバック方式は ECS の System 登録方式に置換される。

---

## 9. 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [LIBINPUT_DESIGN.md](../libinput/LIBINPUT_DESIGN.md) | 入力連携 |
| `os32-game:docs/libchem/LIBCHEM_DESIGN.md` (LIBCHEM_DESIGN.md) | 化学ブリッジ |
| [LIBMATH_DESIGN.md](../libmath/LIBMATH_DESIGN.md) | fix16_t |
| [DEVELOPMENT.md](../../DEVELOPMENT.md) | 技術仕様ガイド |
