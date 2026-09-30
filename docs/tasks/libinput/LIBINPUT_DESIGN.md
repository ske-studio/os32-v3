# libos32input — 入力抽象化ライブラリ設計書

*策定: 2026-04-28*

> この文書は、OS32のゲーム・GUI開発基盤として、物理デバイス（キーボード・マウス・
> 将来のゲームパッド）の入力を論理アクションに変換する抽象化ライブラリ
> `libos32input` の設計思想・API仕様・実装計画を定義する。

---

## 1. 設計背景

### 1.1 なぜ libos32input が必要か

OS32のゲーム開発において、入力処理は現在以下の課題を抱えている:

1. **デバイス直結のハードコーディング** — ゲームロジックが `kbd_is_pressed(0x34)`
   のようにスキャンコードを直接参照しており、キーコンフィグの変更にコード修正が必要
2. **入力コードの散在** — libpyxel の `pyxel_input.c` に入力管理が閉じ込められており、
   libtilemap 単体やGUIシェルからは再利用不可
3. **複数デバイスの非統合** — キーボードとマウスが別々のKAPI関数で取得され、
   「Spaceキーでもマウス左クリックでもジャンプ」といった統合的な入力定義ができない
4. **将来のGUIシェル (v1.1) への備え** — ROADMAPのv1.1「イベントループ基盤」は
   デバイス統合入力層を前提としている

### 1.2 設計思想

- **アクションマッピング方式**: ゲームロジックは物理デバイスを知らず、
  「ジャンプ」「移動X」などの論理アクションだけを参照する
- **ランタイムはC89整数演算**: 値の表現には `fix16_t` (Q16.16) を使用し、
  FPU命令を発行しない
- **KernelAPI経由のデバイスアクセス**: kbd/mouse のポーリングはKAPI関数ポインタ経由
- **libpyxelからの独立**: libpyxelは廃止方向のため、独立ライブラリとして設計。
  libpyxelの `pyxel_input.c` は参考実装として残すが、新規ゲームは本ライブラリを使用

### 1.3 参考にした既存実装

| 実装 | 参考箇所 |
|------|---------|
| `pyxel_input.c` | オンデマンドポーリング方式、フレーム差分管理、ホールドカウンタ |
| `kbd.c` / `kbd.h` | PC-98スキャンコード体系 (0x00-0x7F)、128キービットマップ |
| `mouse.h` | MouseState構造体、バスマウス/シームレスマウス抽象化 |
| Unity InputSystem | 3層構造 (Device/Control/Action)、バインディング方式 |

---

## 2. アーキテクチャ

### 2.1 3層構造

```
┌─────────────────────────────────────────────────┐
│  アクションマッピング層 (Action Mapping Layer)    │
│  「Jump」「MoveX」などの論理アクション             │
│  ゲームロジックはここだけを参照                    │
├─────────────────────────────────────────────────┤
│  統合入力層 (Virtual Device Layer)                │
│  キー/マウス/パッドの入力を fix16_t 値に正規化     │
│  バインディング配列で物理入力→アクションを紐付け  │
├─────────────────────────────────────────────────┤
│  デバイス層 (Device Layer)                        │
│  KAPI: kbd_is_pressed(), mouse_poll() 等          │
│  (既存のOS32カーネルドライバそのまま)              │
└─────────────────────────────────────────────────┘
```

### 2.2 ライブラリ依存関係

```
libos32math  (依存なし — fix16_t, fix16_clamp 等)
     ^
libos32input (math + KAPI)
     ^
     ├── libos32gui   (v1.1 イベントループ基盤)
     ├── libtilemap   (マップデモ等の入力)
     ├── libos32map   (RPGゲーム入力)
     └── ゲーム本体   (アクションベース入力)
```

**重要な制約**:
- libos32input は **libos32gfx, libpyxel に依存しない**
- KernelAPI (`os32api.h`) に依存する (kbd/mouse アクセスのため)
- libos32math に依存する (fix16_t 型、fix16_clamp 等)

### 2.3 ディレクトリ構成

```
programs/libos32input/
    libos32input.h         公開ヘッダ (全API宣言 + 型定義 + 定数)
    input_core.c           初期化, フレーム更新, 終了処理
    input_bind.c           バインディング登録・解除
    input_query.c          アクション状態問い合わせ (pressed/triggered 等)
```

---

## 3. コアデータ構造

### 3.1 デバイス種別

```c
/* 入力デバイス種別 */
#define INP_DEV_KEYBOARD  0
#define INP_DEV_MOUSE     1
#define INP_DEV_GAMEPAD   2   /* 将来用: シリアルブリッジ経由 */
```

### 3.2 アクション状態 (InputActionState)

```c
#define INPUT_MAX_ACTIONS  32   /* 同時管理アクション上限 */

typedef struct {
    fix16_t value;              /* 現在の入力値 (0=未入力, FIX16_ONE=全押し) */
    fix16_t prev_value;         /* 前フレームの値 (トリガー/リリース判定用) */
    u16     hold_frames;        /* 押し続けフレーム数 (リピート判定用) */
    u16     _pad;
} InputActionState;
```

**設計判断**: `id` フィールドは持たない。配列のインデックスがアクションIDに直接対応する。

### 3.3 バインディング (InputBinding)

```c
#define INPUT_MAX_BINDINGS  64  /* 同時バインディング上限 */

typedef struct {
    u8      action_id;          /* 紐付け先アクションID (0-31) */
    u8      device;             /* INP_DEV_* */
    u16     code;               /* キーボード: スキャンコード (0x00-0x7F)
                                   マウス: MOUSE_BTN_LEFT 等 */
    fix16_t scale;              /* 入力値のスケール
                                   デジタル: FIX16_ONE or -FIX16_ONE
                                   アナログ: 任意の倍率 */
} InputBinding;                 /* 合計 8バイト */
```

**メモリ効率**: `InputCode` 構造体を廃し、`device` (u8) + `code` (u16) にフラット化。
PC-98スキャンコードは 0x00-0x7F (128キー)、マウスボタンは 0x01-0x04 なので u16 で十分。

### 3.4 マウスキャッシュ

```c
/* マウスは毎フレーム1回だけpollする (KAPI呼出削減) */
typedef struct {
    i16 x, y;                   /* 現在座標 */
    i16 dx, dy;                 /* 前フレームからの差分 */
    u8  buttons;                /* ボタンビットマスク */
    u8  polled;                 /* このフレームでpoll済みか */
} InputMouseCache;
```

---

## 4. API設計

### 4.1 システム管理

```c
/* 初期化: KAPIポインタを保持、内部配列ゼロクリア */
int  input_init(KernelAPI *api);

/* 終了: 内部状態リセット */
void input_shutdown(void);
```

### 4.2 バインディング管理

```c
/* 物理入力→アクションの紐付けを追加
 * 戻り値: 0=成功, -1=バインディング上限超過
 *
 * 使用例:
 *   input_bind(ACT_JUMP, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);   Space→Jump
 *   input_bind(ACT_JUMP, INP_DEV_MOUSE, MOUSE_BTN_LEFT, FIX16_ONE);
 *   input_bind(ACT_MOVE_X, INP_DEV_KEYBOARD, 0x3C, FIX16_ONE);  右→+1.0
 *   input_bind(ACT_MOVE_X, INP_DEV_KEYBOARD, 0x3B, -FIX16_ONE); 左→-1.0
 */
int  input_bind(int action_id, int device, int code, fix16_t scale);

/* 指定アクションの全バインディングを解除 */
void input_unbind(int action_id);

/* 全バインディングを解除 */
void input_unbind_all(void);
```

### 4.3 フレーム更新

```c
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
```

### 4.4 アクション状態取得

```c
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
```

### 4.5 ユーティリティ

```c
/* 修飾キー状態取得 (kbd_get_modifiers ラッパー)
 * 戻り値: SHIFT_SHIFT | SHIFT_CTRL | SHIFT_CAPS 等のビットマスク
 */
u32 input_modifiers(void);

/* マウス座標取得 (mouse_poll キャッシュから) */
void input_get_mouse(i16 *x, i16 *y);

/* マウス差分取得 */
void input_get_mouse_delta(i16 *dx, i16 *dy);

/* マウスボタン押下判定 (MOUSE_BTN_LEFT 等) */
int input_mouse_btn(int btn);
```

---

## 5. 処理フロー

### 5.1 初期化フロー

```
input_init(api)
  ├── api ポインタを内部保持
  ├── actions[INPUT_MAX_ACTIONS] ゼロクリア
  ├── bindings[INPUT_MAX_BINDINGS] ゼロクリア
  ├── num_bindings = 0
  └── mouse_cache ゼロクリア
```

### 5.2 毎フレーム更新 (`input_update`)

```
input_update()  <- 毎フレーム呼び出し
  │
  ├── for each action (i = 0..31):
  │   ├── actions[i].prev_value = actions[i].value
  │   └── actions[i].value = 0
  │
  ├── mouse_cache.polled = 0  (マウスキャッシュ無効化)
  │
  ├── for each binding (j = 0..num_bindings-1):
  │   │
  │   ├── [KEYBOARD] raw = api->kbd_is_pressed(code) ? FIX16_ONE : 0
  │   │
  │   ├── [MOUSE] ensure_mouse_polled()
  │   │           raw = (cached.buttons & code) ? FIX16_ONE : 0
  │   │
  │   ├── [GAMEPAD] (将来: シリアル経由パケット受信)
  │   │
  │   └── actions[action_id].value += fix16_mul(raw, scale)
  │
  └── for each action (i = 0..31):
      ├── value = fix16_clamp(value, -FIX16_ONE, FIX16_ONE)
      ├── value != 0 → hold_frames++ (上限 0xFFFF)
      └── value == 0 → hold_frames = 0
```

### 5.3 ゲーム側の使用例

```c
/* --- アクションID定義 --- */
#define ACT_MOVE_UP     0
#define ACT_MOVE_DOWN   1
#define ACT_MOVE_LEFT   2
#define ACT_MOVE_RIGHT  3
#define ACT_CONFIRM     4
#define ACT_CANCEL      5
#define ACT_MENU        6

/* --- 初期化 (キーコンフィグ) --- */
void setup_input(KernelAPI *api)
{
    input_init(api);

    /* 方向キー */
    input_bind(ACT_MOVE_UP,    INP_DEV_KEYBOARD, 0x3A, FIX16_ONE);
    input_bind(ACT_MOVE_DOWN,  INP_DEV_KEYBOARD, 0x3D, FIX16_ONE);
    input_bind(ACT_MOVE_LEFT,  INP_DEV_KEYBOARD, 0x3B, FIX16_ONE);
    input_bind(ACT_MOVE_RIGHT, INP_DEV_KEYBOARD, 0x3C, FIX16_ONE);

    /* 決定 = Space or マウス左 */
    input_bind(ACT_CONFIRM, INP_DEV_KEYBOARD, 0x34, FIX16_ONE);
    input_bind(ACT_CONFIRM, INP_DEV_MOUSE, MOUSE_BTN_LEFT, FIX16_ONE);

    /* キャンセル = Escape or マウス右 */
    input_bind(ACT_CANCEL, INP_DEV_KEYBOARD, 0x00, FIX16_ONE);
    input_bind(ACT_CANCEL, INP_DEV_MOUSE, MOUSE_BTN_RIGHT, FIX16_ONE);

    /* メニュー = F1 */
    input_bind(ACT_MENU, INP_DEV_KEYBOARD, 0x62, FIX16_ONE);
}

/* --- ゲームループ --- */
void update(void)
{
    input_update();

    /* デバイスを問わず「決定が押された瞬間」 */
    if (input_triggered(ACT_CONFIRM)) {
        do_confirm();
    }

    /* 方向入力 (押下中) */
    if (input_pressed(ACT_MOVE_UP))    player_move(0, -1);
    if (input_pressed(ACT_MOVE_DOWN))  player_move(0,  1);
    if (input_pressed(ACT_MOVE_LEFT))  player_move(-1, 0);
    if (input_pressed(ACT_MOVE_RIGHT)) player_move( 1, 0);
}
```

---

## 6. KAPI呼出コストの評価

### 6.1 各KAPI関数のコスト

| 関数 | 内部処理 | 推定サイクル数 |
|------|---------|-------------|
| `kbd_is_pressed(scancode)` | ビットマップ参照 (メモリアクセスのみ) | 20-30 |
| `mouse_poll(state)` | 構造体コピー (12バイト) | 50-100 |
| `kbd_get_modifiers()` | u8 変数読取 | 10-20 |

### 6.2 フレームあたりの呼出量

典型的なRPG (バインディング12個 = キーボード10 + マウス2):

```
kbd_is_pressed x 10   = 300 サイクル
mouse_poll x 1        = 100 サイクル (キャッシュにより1回のみ)
合計                  = 400 サイクル

16MHz i386: 400 / 16,000,000 = 0.025ms / フレーム
```

**結論: 全く問題なし。** 現在の pyxel_input.c のオンデマンド方式との差は無視できる。

---

## 7. 将来の拡張設計

### 7.1 コンテキスト切替 (P2)

ゲーム中とメニュー画面でアクションマッピングを切り替える:

```c
#define INPUT_CTX_MAX     4

int  input_save_context(int slot);     /* 現在のバインディングをslotに保存 */
int  input_load_context(int slot);     /* slotからバインディングを復元 */
```

メモリコスト: `InputBinding[64] x 4スロット = 2,048バイト` (必要時のみ確保)

### 7.2 複合キー / 修飾キー対応 (P2)

Ctrl+S などの組み合わせバインディング:

```c
typedef struct {
    u8      action_id;
    u8      device;
    u8      modifier_mask;      /* 要求する修飾キー (SHIFT_CTRL 等、0=不問) */
    u8      _pad;
    u16     code;
    u16     _pad2;
    fix16_t scale;
} InputBindingEx;               /* 拡張版: 12バイト */
```

### 7.3 イベントキュー (P3 — v1.1 GUIシェル向け)

ポーリングベースとは別系統のイベント駆動入力。マウスクリック座標やドラッグ差分など、
GUIウィンドウマネージャが必要とするイベントを時系列キューで管理:

```c
#define INPUT_EVT_KEY_DOWN      1
#define INPUT_EVT_KEY_UP        2
#define INPUT_EVT_MOUSE_MOVE    3
#define INPUT_EVT_MOUSE_DOWN    4
#define INPUT_EVT_MOUSE_UP      5

typedef struct {
    u8  type;           /* INPUT_EVT_* */
    u8  device;
    u16 code;
    i16 x, y;           /* マウス座標 (マウスイベント時のみ) */
    u32 timestamp;      /* get_tick() */
} InputEvent;           /* 12バイト */

#define INPUT_EVT_QUEUE_SIZE  32

int  input_poll_event(InputEvent *evt);  /* 1件取得, 0=キューが空 */
int  input_peek_event(InputEvent *evt);  /* 取り出さずに確認 */
void input_flush_events(void);           /* キューをクリア */
```

### 7.4 ゲームパッド — シリアルブリッジ構想

PC-98実機に現代のUSBゲームパッドを接続する構想。
ハードウェアブリッジ (Arduino/ESP32) でUSB HIDをパースし、
RS-232C経由でOS32にバイナリパケットを送信する。

```
[USBゲームパッド] → [Arduino/ESP32] → RS-232C → [PC-98]
                      USB HIDパース
                      ↓
                   簡易バイナリプロトコル:
                   [SYNC] [buttons:u16] [axis_x:i8] [axis_y:i8] [checksum]
```

**OS32側の設計ポイント**:
- 既存のシリアルドライバ (`serial.c`, KAPI v1から実装済み) を流用
- `INP_DEV_GAMEPAD` はシリアル受信パケットから `fix16_t` 値に変換
- アナログスティック対応時にデッドゾーン処理が必要 → `libos32math` に汎用関数を配置
- ブリッジ側ファームウェアは別リポジトリ管理

**PC-98 ATARIジョイスティックポートとの比較**:

| 方式 | ボタン数 | アナログ | 実機対応 | 備考 |
|------|---------|---------|---------|------|
| ATARIポート (0x4B/0x4D) | 2 | なし | 実機のみ | デジタル方向+A/Bのみ |
| シリアルブリッジ | 無制限 | 対応可 | 実機+エミュ | プロトコル次第で拡張自在 |

> 実装優先度は P3 (当面はID確保のみ)。
> ただし `libos32input` の設計は、この拡張を阻害しないように作る。

---

## 8. リソース使用量の見積もり

| 項目 | サイズ |
|------|--------|
| コード (.text) | ~800B |
| InputActionState[32] | 384B |
| InputBinding[64] | 512B |
| InputMouseCache | 10B |
| 内部変数 (num_bindings, api ptr 等) | ~20B |
| **合計 RAM** | **~1,726B (~2KB)** |

PC-98環境でも余裕をもって収まるサイズ。
libos32chem (~5.5KB), libos32map (~16KB) と比較しても最小クラス。

---

## 9. 設計上の判断ポイント

### Q: アクションIDは `#define` か文字列か？

**`#define` 整数を採用。** 理由:
- C89環境でハッシュマップは過剰
- 配列インデックスで O(1) アクセス
- ゲーム側が `#define ACT_JUMP 0` と宣言するだけで済む
- 32アクション上限はゲーム用途で十分

### Q: `pyxel_btn()` との互換性はどうするか？

**libpyxelは廃止方向のため、互換ラッパーは作らない。**
`pyxel_input.c` は参考実装として残すが、新規ゲームは
`input_pressed()` / `input_triggered()` を直接使用する。

### Q: `input_update` は一括更新か、オンデマンドか？

**一括更新を採用。** 理由:
- バインディング数は最大64個。KAPI呼出コストは 0.025ms/フレームで無視できる
- 一括更新の方がフレーム内の入力状態が一貫する (途中で状態が変わらない)
- オンデマンド方式は pyxel_input.c で検証済みだが、
  バインディングのスケール合算と相性が悪い (未更新のアクションが残る)

### Q: マウスのpollは毎フレーム何回？

**1回のみ (キャッシュ方式)。** 理由:
- `mouse_poll()` は構造体コピーを伴い、kbd_is_pressed より重い
- バインディングで複数のマウスボタンが登録されていても、
  pollは1回だけ行いキャッシュから読む
- `ensure_mouse_polled()` 内部関数でレイジー初期化

---

## 10. 実装フェーズ

### Phase 1: コア実装

- [x] `libos32input.h` ヘッダ作成 (型定義, 定数, 全API宣言)
- [x] `input_core.c` 実装 (init, update, shutdown, マウスキャッシュ)
- [x] `input_bind.c` 実装 (bind, unbind, unbind_all)
- [x] `input_query.c` 実装 (pressed, triggered, released, value, held)
- [x] Makefile 統合 (`LIBINPUT_OBJ`, リンク順序)
- [x] テストプログラム `input_test.c` 作成

### Phase 2: 高度な機能

- [x] コンテキスト切替 (save_context / load_context) — `input_ctx.c`
- [x] 複合キー (修飾キーマスク対応) — `InputBinding.modifier_mask` + `input_bind_ex()`
- [x] マウス座標・差分取得のユーティリティ (P1で実装済み)
- [x] キーコンフィグのファイル保存・読み込み — `input_config.c`

### Phase 3: GUIシェル・ゲームパッド

- [ ] イベントキュー (InputEvent, poll_event)
- [ ] ゲームパッドドライバ (カーネル側シリアルブリッジ)
- [ ] デッドゾーン処理 (`libos32math` に `fix16_deadzone()` 追加)

---

## 11. 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| [LIBMATH_DESIGN.md](../libmath/LIBMATH_DESIGN.md) | libos32math 設計書 (fix16_t 依存先) |
| `os32-game:docs/libchem/LIBCHEM_DESIGN.md` (LIBCHEM_DESIGN.md) | libos32chem 設計書 (同一アーキテクチャパターン) |
| [KAPI_SPEC.md](../../KAPI_SPEC.md) | KernelAPI 仕様書 (kbd/mouse API定義) |
| [ROADMAP.md](../../ROADMAP.md) | v1.1 GUIシェル計画 (イベントループ基盤) |
| [05_drivers.md](../../05_drivers.md) | デバイスドライバ仕様 (kbd.c, mouse.c) |

---

*この設計書は libos32input の実装に先立つ設計ドキュメントであり、*
*実装フェーズの進行に伴い更新される。*
