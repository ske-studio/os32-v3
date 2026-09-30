# FEP 実装状態 (2026-04-27 時点のスナップショット)

OS32 カーネル常駐型 FEP (日本語入力フロントエンドプロセッサ) のアーキテクチャ説明。
**2026-04-27 時点の記述で、その後の進捗 (候補操作、スクロール保護、`ime` コマンド、
描画バックエンド抽象化 `ime_render`、辞書切替、XFER) は反映していない。** 進捗は
[00_INDEX.md](00_INDEX.md) の表が正典。SQLite プールは現在 384KB (CLAUDE.md)。

## アーキテクチャ概要

```
┌───────────────────────────────────────────┐
│         アプリケーション (shell, vz 等)         │
│     ime_getchar() / ime_getkey() を呼び出し     │
├───────────────────────────────────────────┤
│                  kernel/ime.c                   │
│  ┌──────────┬────────────┬──────────────┐  │
│  │ ステート  │ プリエディ │  確定バッファ   │  │
│  │ マシン    │ ット描画   │  管理           │  │
│  └──────────┴────────────┴──────────────┘  │
│        ↓                    ↓                   │
│  ime_romkana.c         ime_dict.c               │
│  (ローマ字→かな)      (SQLite 辞書検索)         │
│                            ↓                   │
│                     sqlite3_*()                  │
│                 (カーネル空間直接呼出)            │
│                            ↓                   │
│                     /db/fep.db                   │
│                  (ext2 上の SQLite DB)           │
├───────────────────────────────────────────┤
│          TVRAM 25行目 (プリエディット UI)        │
└───────────────────────────────────────────┘
```

**設計ポイント:**
- KAPI IPC 経由ではなく、カーネル空間から `sqlite3_*` 関数を直接呼び出す
- 外部プログラム向け KAPI (`db_open` 等) とは独立した辞書アクセスパス
- シングルタスク前提: FEP ON 時はアプリのメインループが停止

SQLite カーネル統合の詳細は
[docs/tasks/sqlite/](../sqlite/00_INDEX.md) を参照。

---

## 実装済みファイル一覧

| ファイル | 役割 | 行数 |
|---------|------|------|
| `kernel/ime.h` | IME 状態構造体 + 公開 API 宣言 | ~128 |
| `kernel/ime.c` | メインロジック (キー処理, プリエディット UI, 公開 API) | ~533 |
| `kernel/ime_dict.c` | SQLite 辞書バックエンド (検索 + 学習 UPSERT) | ~204 |
| `kernel/ime_romkana.c` | ローマ字→ひらがな変換テーブル + エンジン | ~300 |

### ツール

| ファイル | 役割 |
|---------|------|
| `tools/fep_to_sqlite.py` | IPADIC CSV → SQLite DB 変換 (S/M/L バリアント) |
| `tools/analyze_dict.py` | 辞書品質分析 |
| `tools/check_candidates.py` | 候補順確認 |

### アセット

| ファイル | 内容 |
|---------|------|
| `assets/fep.db` | 生成済み辞書 DB (M バリアント, ~5.5MB, 97,514 entries) |
| `assets/fep_l.db` | L バリアント |
| `assets/fep_s.db` | S バリアント |
| `assets/joyo_kanji.txt` | 常用漢字 2,136 字リスト |

---

## 辞書バックエンド

### SQLite スキーマ

```sql
/* システム辞書 (fep_to_sqlite.py が生成) */
CREATE TABLE dict (
    yomi   TEXT NOT NULL,
    kanji  TEXT NOT NULL,
    pos_id INTEGER,
    cost   INTEGER
);
CREATE INDEX idx_yomi ON dict(yomi);

/* ユーザー学習辞書 (ime_dict_open() が自動作成) */
CREATE TABLE dict_user (
    yomi    TEXT NOT NULL,
    kanji   TEXT NOT NULL,
    freq    INTEGER DEFAULT 1,
    last_ts INTEGER DEFAULT 0,
    PRIMARY KEY (yomi, kanji)
);
CREATE INDEX idx_user_yomi ON dict_user(yomi);
```

### 検索モード

| 条件 | モード | SQL | 理由 |
|------|--------|-----|------|
| 読み ≤ 2文字 | 完全一致 | `WHERE yomi = ?1` | 短い読みの前方一致は雑音が多い |
| 読み ≥ 3文字 | 前方一致 | `WHERE yomi >= ?1 AND yomi < ?1 \|\| X'EFBFBF'` | 長い読みは部分入力で候補を出す |

- 前方一致時、完全一致には **-500 コストボーナス** を付与
- ユーザー辞書 (`dict_user`) は `UNION ALL` で結合、コスト `(-10000 - freq)` で常に最優先

### コスト計算 (fep_to_sqlite.py)

| ファクタ | 効果 |
|---------|------|
| 対数スケール正規化 | 高頻度語と低頻度語の差を拡大 |
| 常用漢字ブースト | 2,136字 → コスト 60% 減 |
| 圧縮率ヒューリスティクス | 読み文字数 > 漢字文字数 → コスト 40% 減 |
| カタカナ/ひらがなそのまま表記 | コスト +800 (降格) |

### 学習機構

- 候補確定時に `commit_candidate()` → `ime_dict_learn()` を呼び出し
- `INSERT ... ON CONFLICT DO UPDATE SET freq = freq + 1` (UPSERT)
- 次回検索時に学習エントリがシステム辞書より優先される

---

## 入力フロー

### モード切替

- **Shift + Space** → `ime_toggle()` (ON/OFF トグル)
- ON: ひらがなモード (`IME_MODE_HIRAGANA`)
- OFF: 直接入力 (`IME_MODE_OFF`)
- カタカナモード (`IME_MODE_KATAKANA`) はプログラムから `ime_set_mode()` で設定可能

### キーバインド

| キー | 通常入力中 | 変換候補表示中 |
|------|-----------|--------------|
| 英字 | ローマ字→かな変換 | 候補確定後フォールスルー |
| Space | 変換開始 (最長一致) | 次候補 |
| Enter | かなバッファ直接確定 | 候補確定 |
| ESC | かなバッファクリア | 変換キャンセル |
| BS | 末尾文字削除 | 変換キャンセル |

### 変換アルゴリズム (最長一致法)

1. かなバッファ全体で辞書検索
2. ヒットなし → 1文字縮めて再検索 (UTF-8 文字境界を考慮)
3. ヒットあり → 候補表示、対応する読み部分を `convert_len` として記録
4. 候補確定時 → 変換対象部分だけ消費、残りのかなはバッファに保持

---

## UI 描画 (プリエディット)

**表示位置:** TVRAM 25行目 (行24, 0始まり)

| 状態 | 表示内容 | 色 |
|------|---------|-----|
| モードインジケーター | `[あ]` / `[ア]` | シアン |
| 未確定かな | ひらがな文字列 | 緑 |
| 未確定ローマ字 | アルファベット | 黄 |
| 変換候補 | `▼漢字(01/05)` | 白 (候補) + シアン (番号) |

---

## 公開 API

```c
void ime_init(void);          /* ブート時に1回呼び出し */
void ime_toggle(void);        /* FEP ON/OFF トグル (初回に辞書ロード) */
int  ime_is_active(void);     /* FEP 状態取得 */
void ime_set_mode(int mode);  /* モード設定 (OFF/HIRAGANA/KATAKANA) */
int  ime_get_mode(void);      /* モード取得 */
int  ime_getchar(void);       /* ブロッキング入力 (確定文字を1バイトずつ) */
int  ime_trygetchar(void);    /* ノンブロッキング入力 */
int  ime_getkey(void);        /* kbd_getkey 互換 (scancode << 8 | ascii) */
```

アプリケーションは `kbd_getchar()` の代わりに `ime_getchar()` を呼び出すだけで FEP 機能が透過的に利用できる。

---

## デプロイ

```yaml
# deploy.yaml
- host: assets/fep.db
  guest: /db/fep.db
  tags: [data]
```

辞書パスは `kernel/ime.c` 内の `#define IME_DICT_PATH "/db/fep.db"` で定義。
