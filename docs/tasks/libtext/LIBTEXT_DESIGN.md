# libos32text — テキスト管理ライブラリ設計書

## 1. 概要

RPG/ADV向け「テキスト管理エンジン」の OS32 実装。
メッセージ・会話テキストを **SQLiteデータベース** で一元管理し、
ランタイムでは **タイプライター演出・ページ送り・変数埋め込み** を
C89整数演算で処理する。

### 設計思想

- **テキストデータはSQL**: シナリオライターが C を書かずにテキストを追加・修正可能
- **ランタイムはC**: タイプライター進行・ページ分割は SQL 不使用、純粋 C 処理
- **起動時キャッシュなし**: テキストは表示直前にDBから1件取得 (chem/mapと異なる)
- **描画 (gfx) には依存しない**: テキスト状態管理のみ。描画はゲーム側の責任

### 依存関係

```
libos32db   (KAPI経由SQLite)
     ↑
libos32text (db のみに依存)
     ↑
ゲーム側    (gfx描画はゲーム側/ECSアダプタが担当)
```

`libos32text` は **描画 (gfx) にも ECS にも依存しない**。
テキストの取得・演出状態管理のみを担当し、
画面描画はゲーム側 or ECSアダプタが `text_get_visible()` を参照して行う。

### libos32chem / libos32map との比較

| 項目 | chem | map | **text** |
|------|------|-----|----------|
| DB使用タイミング | 起動時キャッシュ | ロード時キャッシュ | **表示直前に1件取得** |
| RAMキャッシュ | ルール配列 64件 | タイルデータ全体 | **表示中の1メッセージのみ** |
| 毎フレーム処理 | 温度・状態遷移 | なし | **タイプライター進行** |
| DB依存 | libos32db | libos32db | libos32db |
| GFX依存 | なし | なし | なし |

**RAMフットプリントが極めて小さい** のが特徴。
テキストは一度に1件しか表示しないため、大量キャッシュ不要。

---

## 2. コアデータ構造

### 2.1 テキストスロット

```c
#define TEXT_MAX_SLOTS     4    /* 同時管理スロット上限 */
#define TEXT_BUF_SIZE    256    /* 1メッセージの最大バイト長 (UTF-8) */
#define TEXT_MAX_PAGES     8    /* 1メッセージの最大ページ数 */
#define TEXT_MAX_VARS     16    /* 変数埋め込み上限 */
```

複数スロットの理由:
- スロット0: メインメッセージウィンドウ
- スロット1: NPC名前表示
- スロット2: システムメッセージ (アイテム取得等)
- スロット3: 予備

### 2.2 テキスト状態

```c
/* テキスト表示状態 */
#define TEXT_STATE_IDLE       0  /* 非表示 */
#define TEXT_STATE_TYPING     1  /* タイプライター進行中 */
#define TEXT_STATE_WAIT       2  /* 入力待ち (ページ末尾) */
#define TEXT_STATE_DONE       3  /* 全ページ表示完了 */

/* スロット構造体 */
typedef struct {
    char buf[TEXT_BUF_SIZE];     /* テキストバッファ (UTF-8) */
    u16  total_len;              /* テキスト全体のバイト長 */
    u16  visible_len;            /* 現在表示済みのバイト長 */
    u16  page_offsets[TEXT_MAX_PAGES]; /* 各ページの開始バイト位置 */
    u8   page_count;             /* 総ページ数 */
    u8   current_page;           /* 現在ページ (0始まり) */
    u8   state;                  /* TEXT_STATE_* */
    u8   speed;                  /* 表示速度 (フレーム/文字, 1=最速) */
    u8   counter;                /* フレームカウンタ */
    u8   active;                 /* 0=未使用, 1=使用中 */
} TextSlot;                      /* ~286B */
```

### 2.3 変数テーブル

ゲーム側が設定する変数を、テキスト内の `{0}` `{1}` で参照:

```c
typedef struct {
    char value[32];              /* 展開後の文字列 */
} TextVar;
```

---

## 3. SQLテーブル設計

ファイル: `/db/text.db` (ゲームタイトルごとに別DB可)

```sql
/* メッセージテーブル */
CREATE TABLE messages (
    id       INTEGER PRIMARY KEY,
    group_id INTEGER NOT NULL DEFAULT 0,  /* グループ (シーン/NPC等) */
    seq      INTEGER NOT NULL DEFAULT 0,  /* グループ内の表示順 */
    speaker  TEXT,                         /* 話者名 (NULL=ナレーション) */
    text     TEXT NOT NULL,               /* メッセージ本文 (UTF-8) */
    speed    INTEGER NOT NULL DEFAULT 2   /* 表示速度 (フレーム/文字) */
);

/* グループ定義 (任意) */
CREATE TABLE msg_groups (
    id       INTEGER PRIMARY KEY,
    name     TEXT NOT NULL,               /* デバッグ用名前 */
    category TEXT                         /* scene/npc/system/item 等 */
);
```

### テキスト内の制御記法

本文中に以下の制御文字を埋め込み可能:

| 記法 | 意味 |
|------|------|
| `\p` | ページ区切り (入力待ち → 次ページ) |
| `\w30` | 30フレーム一時停止 |
| `{0}` `{1}` ... | 変数展開 (text_set_var で設定) |

例: `「{0}は {1} を手に入れた！\pおめでとう！」`

---

## 4. API設計

### 4.1 システム管理

```c
/* 初期化: DBを開く */
int  text_init(const char *db_path);

/* 終了: DB接続クローズ */
void text_shutdown(void);
```

### 4.2 メッセージ操作

```c
/* メッセージをDBから取得してスロットにロード
 * slot: スロット番号 (0~3)
 * msg_id: messages テーブルの id
 * 戻り値: 0=成功, -1=スロット範囲外, -2=DB取得失敗 */
int  text_load(int slot, u16 msg_id);

/* グループの先頭メッセージをロード (連続会話用)
 * 戻り値: 0=成功, 負=エラー */
int  text_load_group(int slot, u16 group_id);

/* 次のメッセージをロード (同一グループ内の seq+1)
 * 戻り値: 0=成功, -1=グループ終端 */
int  text_next_message(int slot);

/* スロットを閉じる (状態リセット) */
void text_close(int slot);
```

### 4.3 テキスト演出制御

```c
/* 毎フレーム呼び出し — タイプライター進行
 * 全スロットの TYPING 状態を更新 */
void text_update(void);

/* 全文即時表示 (Bボタンスキップ) */
void text_skip(int slot);

/* 次ページ送り (入力待ち状態で呼ぶ)
 * 戻り値: 0=次ページ表示開始, -1=最終ページ完了 */
int  text_advance(int slot);

/* 表示速度変更 (フレーム/文字, 1=最速) */
void text_set_speed(int slot, u8 speed);
```

### 4.4 状態取得 (描画側が参照)

```c
/* 現在の状態を取得 */
u8   text_get_state(int slot);

/* 表示すべきテキストのポインタと長さ
 * 現在ページの先頭から visible_len までを返す */
const char *text_get_visible(int slot, int *out_len);

/* 話者名を取得 (NULL=ナレーション) */
const char *text_get_speaker(int slot);

/* 現在ページ / 総ページ数 */
u8   text_get_page(int slot);
u8   text_get_page_count(int slot);
```

### 4.5 変数設定

```c
/* 変数テーブルに値を設定
 * var_id: 0~15
 * value: 展開文字列 (例: プレイヤー名) */
void text_set_var(int var_id, const char *value);
```

### 4.6 コールバック

```c
/* メッセージ完了時コールバック (全ページ表示後) */
typedef void (*text_done_callback)(int slot, u16 msg_id);
void text_set_done_callback(text_done_callback cb);
```

### 4.7 デバッグ

```c
/* スロット状態ダンプ (kprintf経由) */
void text_debug_dump(int slot);
```

---

## 5. 処理フロー

### 5.1 メッセージ表示フロー

```
text_load(0, 42)
  ├── db_query("SELECT text, speaker, speed FROM messages WHERE id=42")
  ├── 変数展開 ({0} → TextVar[0].value)
  ├── ページ分割 (\p でページ境界を算出)
  ├── page_offsets[] にセット
  └── state = TEXT_STATE_TYPING, counter = 0

text_update()  ← 毎フレーム
  ├── counter++
  ├── counter >= speed ?
  │   ├── visible_len += 次の1文字のバイト数 (UTF-8対応)
  │   ├── 制御文字チェック (\w → 一時停止, \p → WAIT)
  │   └── counter = 0
  └── ページ末尾到達 → state = TEXT_STATE_WAIT

[ゲーム側: ボタン押下検出]
text_advance(0)
  ├── current_page++
  ├── 次ページあり → state = TYPING, visible_len リセット
  └── 最終ページ → state = DONE, callback 発火

text_close(0)
  └── state = IDLE, active = 0
```

### 5.2 連続会話フロー

```
text_load_group(0, 5)     /* グループ5の seq=0 をロード */
  → 表示 → DONE

text_next_message(0)       /* グループ5の seq=1 をロード */
  → 表示 → DONE

text_next_message(0)       /* -1 = グループ終端 */
```

---

## 6. ディレクトリ構造

```
programs/libos32text/
  ├── libos32text.h         公開ヘッダ (型定義, 定数, 全API)
  ├── text_core.c           初期化, DB取得, スロット管理
  ├── text_update.c         タイプライター進行, ページ制御
  └── text_var.c            変数テーブル, 変数展開処理

tools/
  └── text_db_init.py       テストデータ生成 (→ /db/text.db)

programs/tests/
  ├── text_test.c           単体テスト (GFX不要, シリアル出力)
  └── text_demo.c           ビジュアルデモ (メッセージウィンドウ)
```

---

## 7. ECS連携 (アダプタ方式)

`libos32chem` の `CompChem` + `sys_chem_sync` と同じパターン:

```c
/* libos32ecs 側に追加 */
typedef struct {
    i8   text_slot;          /* libos32text のスロット番号 (-1=未接続) */
} CompText;                  /* 1B */

#define COMP_TEXT  0x0400u    /* カスタムコンポーネント (bit 10) */

/* アダプタ関数型 */
typedef void (*ecs_text_adapter_fn)(ecs_entity_t e, i8 text_slot);

/* ゲーム側のアダプタ実装例 */
void my_text_adapter(ecs_entity_t e, i8 slot) {
    u8 state = text_get_state(slot);
    /* state に応じてメッセージウィンドウ描画 */
}
```

ECS側は `text_slot` の数値だけ持ち、`libos32text` への直接依存はない。

---

## 8. 使用リソース見積もり

| 項目 | サイズ |
|------|--------|
| コード (.text) | ~1.5KB |
| TextSlot[4] | 1,144B |
| TextVar[16] | 512B |
| **合計 RAM** | **~3.2KB** |
| text.db (ディスク) | ゲーム依存 (~2-8KB) |

libos32chem (~5.5KB) より小さい。PC-98 環境に余裕で収まる。

---

## 9. 描画層について

`libos32text` は描画しない。描画はゲーム側の責任。

ゲーム側の描画例 (参考):
```c
/* メインループ内 */
if (text_get_state(0) != TEXT_STATE_IDLE) {
    int len;
    const char *vis = text_get_visible(0, &len);
    const char *speaker = text_get_speaker(0);

    /* メッセージウィンドウ枠描画 (libos32gfx) */
    gfx_fill_rect(16, 300, 608, 80, 1);
    gfx_rect(16, 300, 608, 80, 7);

    /* 話者名 */
    if (speaker)
        kcg_draw_utf8(24, 288, speaker, 15, 1);

    /* テキスト本文 (表示済み部分のみ) */
    /* vis は NULL終端ではなく len バイト */
    kcg_draw_utf8_n(24, 310, vis, len, 7, 0xFF);

    /* 入力待ちカーソル */
    if (text_get_state(0) == TEXT_STATE_WAIT)
        gfx_putchar(600, 370, 'v', 7);  /* ▼ */
}
```

メッセージウィンドウの枠描画・ワードラップなどの
汎用部品が必要になれば `libos32gfx/text/gfx_msgbox.c` に追加する。

---

## 10. 実装フェーズ

### Phase 1: コアエンジン

- [x] `libos32text.h` ヘッダ作成 (型定義, 定数, 全API宣言)
- [x] `text_core.c` 実装 (init/shutdown, DB取得, スロット管理)
- [x] `text_update.c` 実装 (タイプライター進行, ページ制御)
- [x] `text_var.c` 実装 (変数テーブル, 展開処理)
- [x] Makefile 統合 (`LIBTEXT_OBJ`, リンク順序)
- [x] `tools/text_db_init.py` テストデータ生成
- [x] `text_test.c` テストプログラム (GFX不要)
- [x] ビルド・NP21/W実機テスト

### Phase 2: グループ会話 + デモ

- [x] `text_load_group()` / `text_next_message()` 実装
- [x] `text_demo.c` ビジュアルデモ (メッセージウィンドウ描画)
- [x] NP21/W実機テスト

### Phase 3: ECS連携 + ゲーム統合 *(保留)*

- [ ] `CompText` + `sys_text_sync` アダプタ実装 (libos32ecs)
- [ ] ゲームプロトタイプでの統合テスト

---

## 11. 設計上の判断ポイント

### Q: テキストをRAMキャッシュすべきか？

**キャッシュしない (1件ずつDB取得)**。理由:
- テキストは一度に1件しか表示しない (chemのように全件同時参照しない)
- メッセージ表示はプレイヤー入力待ちが挟まるため、DB取得の遅延は体感不可
- RAMを節約できる (テキスト全件キャッシュは数KB〜数十KBになりうる)

### Q: スロット数 4 は妥当か？

RPGの同時テキスト表示は通常 1-2 (メインウィンドウ + システムメッセージ)。
4スロットは予備含めて十分。不足時は `TEXT_MAX_SLOTS` を増やすだけ。

### Q: UTF-8の1文字進行はどう実装する？

`lib/utf8.c` の `utf8_decode()` を使い、
1バイト文字 (ASCII) と マルチバイト文字 (日本語) の
バイト数を正しく判定してから `visible_len` を加算する。

---

## 12. 関連ドキュメント

| ドキュメント | 内容 |
|-------------|------|
| `os32-game:docs/libchem/LIBCHEM_DESIGN.md` (LIBCHEM_DESIGN.md) | libos32chem 設計書 (同格ライブラリ) |
| [LIBMATH_DESIGN.md](../libmath/LIBMATH_DESIGN.md) | libos32math 設計書 |
| [KAPI_SPEC.md](../../KAPI_SPEC.md) | KernelAPI 仕様書 (DB接続) |
